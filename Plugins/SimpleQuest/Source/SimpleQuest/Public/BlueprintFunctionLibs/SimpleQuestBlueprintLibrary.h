// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Events/QuestEventBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
#include "SimpleQuestBlueprintLibrary.generated.h"

class UQuestEventSubscription;
class UQuestlineGraph;
class UQuestManagerSubsystem;
class UWorldStateSubsystem;

UCLASS()
class SIMPLEQUEST_API USimpleQuestBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // -------------------------------------------------------------------------------------------------------------
    // Quest state queries: read directly from WorldState and/or QuestState
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestLive(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestCompleted(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestPendingGiver(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestResolvedWith(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static int32 GetQuestCompletionCount(const UObject* WorldContext, FGameplayTag QuestTag);

    // -------------------------------------------------------------------------------------------------------------
    // Quest actions: publish to the signal bus; designer never touches the bus
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void DeactivateQuest(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void GiveQuest(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void ActivateQuest(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void SetQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void ClearQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void ResolveQuest(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag, bool bOverrideExisting = false);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void StartQuestline(const UObject* WorldContext, TSoftObjectPtr<UQuestlineGraph> QuestlineGraph);

    // -------------------------------------------------------------------------------------------------------------
    // World state: general fact store, for power users and external prereqs
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|World State", meta = (WorldContext = "WorldContext"))
    static void AddWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|World State", meta = (WorldContext = "WorldContext"))
    static void RemoveWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|World State", meta = (WorldContext = "WorldContext"))
    static bool HasWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag);

private:
    static UWorldStateSubsystem* GetWorldState(const UObject* WorldContext);
    static USignalSubsystem* GetSignalSubsystem(const UObject* WorldContext);
    static UQuestManagerSubsystem* GetQuestManager(const UObject* WorldContext);

public:
    
    /**
     * C++ one-liner for subscribing to a quest event. Resolves the SignalSubsystem from the world context, subscribes
     * the listener/callback on QuestTag, returns the FDelegateHandle for explicit unbind. Returns an invalid handle if
     * the subsystem can't be resolved or the tag isn't registered. Same silent-failure contract as the BP async action.
     *
     * TEvent is constrained by the CQuestEvent concept to any FQuestEventBase-derived struct published on the quest's
     * tag channel: FQuestStartedEvent, FQuestEndedEvent, FQuestEnabledEvent, FQuestDeactivatedEvent, etc. Passing an
     * unrelated type fails to compile with a clear concept-violation diagnostic.
     */
    template<CQuestEvent TEvent, typename TObject>
    static FDelegateHandle SubscribeToQuestEvent(UObject* WorldContextObject, const FGameplayTag& QuestTag, TObject* Listener, void (TObject::* Callback)(FGameplayTag, const TEvent&))
    {
        if (!FQuestStateTagUtils::IsTagRegisteredInRuntime(QuestTag)) return FDelegateHandle();
        if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
        {
            return Signals->SubscribeMessage<TEvent>(QuestTag, Listener, Callback);
        }
        return FDelegateHandle();
    }

    /**
     * Subscribe to a quest's lifecycle events. Wire any of the four output exec pins you care about:
     *  - On Activated — quest is enabled and ready (may be waiting on a giver).
     *  - On Started — quest is actively running; objectives are live.
     *  - On Completed — quest resolved. The Outcome Tag output tells you which outcome fired — switch on it to branch
     *                                      by Victory / Defeat / Negotiated / etc.
     *  - On Deactivated — quest was blocked or torn down without completing.
     *
     * Hierarchical subscription: pass a parent tag like SimpleQuest.Quest.MyLine and you'll receive events for every
     * descendant quest under it (SimpleQuest.Quest.MyLine.Step1, SimpleQuest.Quest.MyLine.Step2, ...). Every pin fires
     * every time a matching event arrives — not one-shot.
     *
     * Catch-up: if the quest already reached one of these states before you subscribed, the matching pin fires immediately
     * on bind. Late binders aren't left waiting on events that already happened.
     *
     * Context output carries the full event context — Triggered Actor, Instigator, Node Info, Custom Data — so you don't
     * need a separate lookup for who triggered the event or what payload came with it.
     *
     * The subscription persists until you call Cancel on the returned node or the Game Instance tears down. Typical pattern:
     * bind in Begin Play, wire pins into your UI / rewards / progression logic, optionally call Cancel from End Play if you
     * want tighter per-actor lifetime.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Events",
        meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject",
                HidePin = "WorldContextObject,ExposedEvents", DefaultToSelf = "WorldContextObject",
                DisplayName = "Bind To Quest Event"))
    static UQuestEventSubscription* BindToQuestEvent(UObject* WorldContextObject, FGameplayTag QuestTag, UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/SimpleQuest.EQuestEventTypes")) int32 ExposedEvents = 255);

    /** Companion unbind — pairs with BindToQuestEvent's returned handle. Safe no-op if the handle is invalid. */
    static void UnsubscribeFromQuestEvent(UObject* WorldContextObject, const FGameplayTag& QuestTag, FDelegateHandle Handle);

};
