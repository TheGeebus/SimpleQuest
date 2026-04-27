// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Quests/Types/QuestEventContext.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "QuestEventSubscription.generated.h"

struct FQuestGiveBlockedEvent;
struct FQuestDisabledEvent;
struct FQuestActivatedEvent;
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
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EQuestEventTypes : uint16
{
    None        = 0       UMETA(Hidden),
    Activated   = 1 << 0,
    Enabled     = 1 << 1,
    Disabled    = 1 << 2,
    GiveBlocked = 1 << 3,
    Started     = 1 << 4,
    Progress    = 1 << 5,
    Completed   = 1 << 6,
    Deactivated = 1 << 7,
    Blocked     = 1 << 8,
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

/** Activated variant — adds the PrereqStatus payload so designers don't need to query separately. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FQuestSubscriptionActivatedDelegate,
    FGameplayTag, QuestTag, FQuestEventContext, Context, FQuestPrereqStatus, PrereqStatus);

/**
 * Started variant — adds the GiverActor payload. Populated when the quest was given via a giver; null when
 * the quest started from a non-giver activation path.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FQuestSubscriptionStartedDelegate,
    FGameplayTag, QuestTag, FQuestEventContext, Context, AActor*, GiverActor);

/**
 * Give-blocked variant — carries the structured blocker array and the giver actor that initiated the
 * refused attempt. AActor* (raw pointer) for BP friendliness; the underlying TWeakObjectPtr is resolved
 * in the handler before broadcast.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FQuestSubscriptionGiveBlockedDelegate,
    FGameplayTag, QuestTag, const TArray<FQuestActivationBlocker>&, Blockers, AActor*, GiverActor);

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
    /**
     * Fires when execution reaches a giver-gated quest. PrereqStatus describes whether the quest is
     * currently accept-ready. Designers branch on PrereqStatus.bSatisfied to decide UI affordance.
     */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionActivatedDelegate OnActivated;

    /** Fires when a giver-gated quest becomes accept-ready (Activated AND prereqs satisfy). */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnEnabled;

    /**
     * Fires when an accept-ready quest becomes no-longer-ready (satisfied to unsatisfied transition). Symmetric
     * partner to OnEnabled; rare in practice.
     */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnDisabled;

    /**
     * Fires when a give attempt was refused. Carries blocker array + the giver actor that initiated
     * the attempt. Always-subscribed (not per-attempt one-shot like the giver component's path) - this
     * is the global / observer subscription point for refused gives.
     */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionGiveBlockedDelegate OnGiveBlocked;

    // ── Run Phase ─────────────────────────────────────────────────────────────────────────────────
    /** Fires when the subscribed quest enters the Live state. Its objectives are bound. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionStartedDelegate  OnStarted;

    /** Fires on objective progress ticks during the Live phase. Transient: no catch-up. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnProgress;

    // ── End Phase ─────────────────────────────────────────────────────────────────────────────────
    /** Fires when the subscribed quest resolves with an outcome. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionCompletedDelegate OnCompleted;

    /** Fires when the subscribed quest is deactivated before completing. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnDeactivated;

    /** Fires when the quest enters Blocked state via a SetBlocked utility node. */
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

    FDelegateHandle ActivatedHandle;
    FDelegateHandle EnabledHandle;
    FDelegateHandle DisabledHandle;
    FDelegateHandle GiveBlockedHandle;
    FDelegateHandle StartedHandle;
    FDelegateHandle ProgressHandle;
    FDelegateHandle EndedHandle;
    FDelegateHandle DeactivatedHandle;

    bool bCancelled = false;

    /**
     * Per-phase "we already broadcast this lifecycle live" guards. Catch-up skips any phase that already
     * fired through the live path during the one-tick deferral window. No flags for Disabled / GiveBlocked /
     * Given / Progress — those are transient or don't have catch-up semantics.
     */
    bool bSawLiveActivated = false;
    bool bSawLiveEnabled = false;
    bool bSawLiveStarted = false;
    bool bSawLiveCompleted = false;
    bool bSawLiveDeactivated = false;
    bool bSawLiveBlocked = false;

    void HandleActivated(FGameplayTag Channel, const FQuestActivatedEvent& Event);
    void HandleEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event);
    void HandleDisabled(FGameplayTag Channel, const FQuestDisabledEvent& Event);
    void HandleGiveBlocked(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event);
    void HandleStarted(FGameplayTag Channel, const FQuestStartedEvent& Event);
    void HandleProgress(FGameplayTag Channel, const FQuestProgressEvent& Event);
    void HandleEnded(FGameplayTag Channel, const FQuestEndedEvent& Event);
    void HandleDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);

    void UnbindAll();
    void RunCatchUp(USignalSubsystem* Signals, UWorldStateSubsystem* WorldState);

    /** Convenience: is the given event flag set in the exposure mask? */
    FORCEINLINE bool IsExposed(EQuestEventTypes Flag) const
    {
        return (ExposedEventsMask & static_cast<int32>(Flag)) != 0;
    }

    USignalSubsystem* ResolveSignalSubsystem() const;
    UWorldStateSubsystem* ResolveWorldStateSubsystem() const;
    UQuestStateSubsystem* ResolveQuestStateSubsystem() const;
};