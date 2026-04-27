// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Quests/Types/QuestEventContext.h"
#include "Subsystems/QuestResolutionSubsystem.h"
#include "QuestEventSubscription.generated.h"

struct FQuestEnabledEvent;
struct FQuestStartedEvent;
struct FQuestEndedEvent;
struct FQuestDeactivatedEvent;
struct FQuestGivenEvent;
struct FQuestProgressEvent;
class USignalSubsystem;
class UWorldStateSubsystem;

/**
 * Bitflag mask of which lifecycle events the BindToQuestEvent K2 node has exposed. Both the K2 node's per-flag
 * Details-panel checkboxes and the factory's hidden ExposedEvents parameter use these bits — the proxy gates
 * SubscribeMessage calls and catch-up branches by mask, so unexposed events incur zero subscription cost.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EQuestEventTypes : uint8
{
    None        = 0       UMETA(Hidden),
    Activated   = 1 << 0,
    Started     = 1 << 1,
    Completed   = 1 << 2,
    Deactivated = 1 << 3,
    Given       = 1 << 4,
    Progress    = 1 << 5,
    Blocked     = 1 << 6,
};
ENUM_CLASS_FLAGS(EQuestEventTypes);

/**
 * BP-facing delegate for quest lifecycle events other than completion. Matches the UQuestWatcherComponent pattern
 * but carries the full FQuestEventContext so designers reach TriggeredActor, Instigator, NodeInfo, CustomData
 * without a separate lookup.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FQuestSubscriptionLifecycleDelegate,
    FGameplayTag, QuestTag, FQuestEventContext, Context);

/**
 * Completion variant — adds the OutcomeTag. Designers typically branch on OutcomeTag with a Switch or equality
 * check rather than filtering at subscription time, matching the watcher's post-Piece-C pattern.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FQuestSubscriptionCompletedDelegate,
    FGameplayTag, QuestTag, FGameplayTag, OutcomeTag, FQuestEventContext, Context);

/**
 * Async BP action — "Bind To Quest Event". Subscribes to the lifecycle channels selected via the K2 node's
 * Details-panel checkboxes (or the factory's ExposedEvents bitmask) on the given tag and stays bound until
 * Cancel() is called or the GameInstance is torn down.
 *
 * Because USignalSubsystem publishes hierarchically, subscribing on a parent tag (e.g., "SimpleQuest.Quest.MyLine")
 * receives events from every descendant quest tag — each child's lifecycle will fire this action's output pins.
 * Pins fire once per matching live event, not one-shot.
 *
 * Catch-up: on Activate(), any quest-state fact already asserted for QuestTag at subscription time fires the
 * corresponding pin immediately (mirrors UQuestWatcherComponent::RegisterQuestWatcher). Catch-up runs once;
 * subsequent events flow through the live subscriptions. Catch-up is also gated by ExposedEventsMask — phases
 * the K2 node didn't expose are skipped.
 *
 * Designers who want tighter lifetime (cancel on BP actor destruction, etc.) should call Cancel() explicitly from
 * EndPlay or equivalent. Otherwise the action lives for the GameInstance's lifetime.
 */
UCLASS(BlueprintType, meta = (ExposedAsyncProxy = "Subscription"))
class SIMPLEQUEST_API UQuestEventSubscription : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    // ── Offer Phase ───────────────────────────────────────────────────────────────────────────────
    /** Fires when execution reaches a giver-gated quest. Carries prereq evaluation in the lifecycle context. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnActivated;

    /** Fires when a player accepts the quest from a giver. Transient — no catch-up; bind before the give. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnGiven;

    // ── Run Phase ─────────────────────────────────────────────────────────────────────────────────
    /** Fires when the subscribed quest enters the Live state — its objectives are bound and ticking. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnStarted;

    /** Fires on objective progress ticks during the Live phase. Context.CompletionData carries CurrentCount /
     *  RequiredCount / TriggeredActor / Instigator. Transient — no catch-up. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnProgress;

    // ── End Phase ─────────────────────────────────────────────────────────────────────────────────
    /** Fires when the subscribed quest resolves with an outcome. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionCompletedDelegate OnCompleted;

    /** Fires when the subscribed quest is deactivated before completing (interrupt, abandon). */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnDeactivated;

    /** Fires when the quest enters the Blocked state via a SetBlocked utility node. Co-fires with OnDeactivated
     *  for the same transition — designer reads OnBlocked when the distinction between Blocked and other
     *  deactivation reasons matters. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnBlocked;

    /**
     * Plain C++ initializer used by the BP library's factory wrapper. Not a UFUNCTION — the library owns the
     * BP-facing entry point so UK2Node_AsyncAction's subclass iteration doesn't auto-register a duplicate
     * palette entry for us.
     */
    void InitFromFactory(UObject* InWorldContextObject, FGameplayTag InQuestTag, int32 InExposedEventsMask)
    {
        WorldContextObjectWeak = InWorldContextObject;
        QuestTag = InQuestTag;
        ExposedEventsMask = InExposedEventsMask;
    }

    /** Unbind from all channels and mark the action ready to destroy. Safe no-op if already cancelled. */
    UFUNCTION(BlueprintCallable, Category = "Quest|Events")
    void Cancel();

    virtual void Activate() override;

private:
    UPROPERTY() TWeakObjectPtr<UObject> WorldContextObjectWeak;
    FGameplayTag QuestTag;
    int32 ExposedEventsMask = 0;

    FDelegateHandle EnabledHandle;
    FDelegateHandle StartedHandle;
    FDelegateHandle EndedHandle;
    FDelegateHandle DeactivatedHandle;
    FDelegateHandle GivenHandle;
    FDelegateHandle ProgressHandle;

    bool bCancelled = false;

    /** Per-phase "we already broadcast this lifecycle live" guards. Catch-up skips any phase that already fired
     *  through the live path during the one-tick deferral window. No flags for Given/Progress — they are
     *  transient and have no catch-up branch. */
    bool bSawLiveActivated = false;
    bool bSawLiveStarted = false;
    bool bSawLiveCompleted = false;
    bool bSawLiveDeactivated = false;
    bool bSawLiveBlocked = false;

    void HandleEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event);
    void HandleStarted(FGameplayTag Channel, const FQuestStartedEvent& Event);
    void HandleEnded(FGameplayTag Channel, const FQuestEndedEvent& Event);
    void HandleDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
    void HandleGiven(FGameplayTag Channel, const FQuestGivenEvent& Event);
    void HandleProgress(FGameplayTag Channel, const FQuestProgressEvent& Event);

    void UnbindAll();
    void RunCatchUp(USignalSubsystem* Signals, UWorldStateSubsystem* WorldState);

    /** Convenience: is the given event flag set in the exposure mask? */
    FORCEINLINE bool IsExposed(EQuestEventTypes Flag) const
    {
        return (ExposedEventsMask & static_cast<int32>(Flag)) != 0;
    }

    USignalSubsystem* ResolveSignalSubsystem() const;
    UWorldStateSubsystem* ResolveWorldStateSubsystem() const;
    UQuestResolutionSubsystem* ResolveQuestResolutionSubsystem() const;
};