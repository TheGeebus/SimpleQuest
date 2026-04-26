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
class USignalSubsystem;
class UWorldStateSubsystem;

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
 * Async BP action — "Bind To Quest Event". Subscribes to all four quest lifecycle channels (Enabled, Started,
 * Ended, Deactivated) on the given tag and stays bound until Cancel() is called or the GameInstance is torn down.
 *
 * Because USignalSubsystem publishes hierarchically, subscribing on a parent tag (e.g., "SimpleQuest.Quest.MyLine") receives
 * events from every descendant quest tag — each child's lifecycle will fire this action's output pins. Pins fire
 * once per matching live event, not one-shot.
 *
 * Catch-up: on Activate(), any quest-state fact already asserted for QuestTag at subscription time fires the
 * corresponding pin immediately (mirrors UQuestWatcherComponent::RegisterQuestWatcher). Catch-up runs once;
 * subsequent events flow through the live subscriptions.
 *
 * Designers who want tighter lifetime (cancel on BP actor destruction, etc.) should call Cancel() explicitly from
 * EndPlay or equivalent. Otherwise the action lives for the GameInstance's lifetime.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API UQuestEventSubscription : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    
    /** Fires every time the subscribed tag's hierarchy publishes an enabled event (quest becomes givable / awaits giver). */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnActivated;

    /** Fires every time the subscribed tag's hierarchy publishes a started event (quest actually activates). */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnStarted;

    /** Fires every time the subscribed tag's hierarchy publishes a completion event. Carries OutcomeTag. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionCompletedDelegate OnCompleted;

    /** Fires every time the subscribed tag's hierarchy publishes a deactivated / blocked event. */
    UPROPERTY(BlueprintAssignable)
    FQuestSubscriptionLifecycleDelegate OnDeactivated;

    /**
     * Plain C++ initializer used by the BP library's factory wrapper. Not a UFUNCTION — the library owns the
     * BP-facing entry point so UK2Node_AsyncAction's subclass iteration doesn't auto-register a duplicate
     * palette entry for us.
     */
    void InitFromFactory(UObject* InWorldContextObject, FGameplayTag InQuestTag)
    {
        WorldContextObjectWeak = InWorldContextObject;
        QuestTag = InQuestTag;
    }

    /** Unbind from all channels and mark the action ready to destroy. Safe no-op if already cancelled. */
    UFUNCTION(BlueprintCallable, Category = "Quest|Events")
    void Cancel();

    virtual void Activate() override;

private:
    UPROPERTY() TWeakObjectPtr<UObject> WorldContextObjectWeak;
    FGameplayTag QuestTag;

    FDelegateHandle EnabledHandle;
    FDelegateHandle StartedHandle;
    FDelegateHandle EndedHandle;
    FDelegateHandle DeactivatedHandle;

    bool bCancelled = false;

    /**
     * Per-phase "we already broadcast this lifecycle live" guards. Set inside the corresponding Handle* the first
     * time a live signal fires for that phase; checked in RunCatchUp so the deferred catch-up skips any phase
     * that already broadcast through the live path during the one-tick deferral window. Closes the otherwise-
     * narrow possibility of double-broadcasting the same state transition (live + catch-up) for a listener.
     */
    bool bSawLiveActivated = false;
    bool bSawLiveStarted = false;
    bool bSawLiveCompleted = false;
    bool bSawLiveDeactivated = false;

    void HandleEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event);
    void HandleStarted(FGameplayTag Channel, const FQuestStartedEvent& Event);
    void HandleEnded(FGameplayTag Channel, const FQuestEndedEvent& Event);
    void HandleDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);

    void UnbindAll();
    void RunCatchUp(USignalSubsystem* Signals, UWorldStateSubsystem* WorldState);

    USignalSubsystem* ResolveSignalSubsystem() const;
    UWorldStateSubsystem* ResolveWorldStateSubsystem() const;
    UQuestResolutionSubsystem* ResolveQuestResolutionSubsystem() const;

};

