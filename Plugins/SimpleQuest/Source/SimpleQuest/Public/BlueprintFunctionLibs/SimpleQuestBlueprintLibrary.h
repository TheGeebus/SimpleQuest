// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Events/QuestEventBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestTagComposer.h"
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
    /**
     * Subscribe to a quest's lifecycle events. Configure which exec pins to expose via the checkboxes in
     * the Details panel — defaults to On Enabled / On Started / On Completed; opt in to others as needed.
     *
     * Available events organized by phase:
     *
     *  Offer phase:
     *   - On Activated — quest reached a giver-gated waypoint. Prereq Status says whether prereqs are met.
     *   - On Enabled — quest became accept-ready (Activated AND prereqs satisfy).
     *   - On Disabled — accept-ready quest became no-longer-ready (NOT-prereq edge cases; rare).
     *   - On Give Blocked — a give attempt was refused. Blockers carries the structured reasons.
     *
     *  Run phase:
     *   - On Started — quest entered Live state; objectives are bound and ticking.
     *   - On Progress — objective progress tick (transient, no catch-up).
     *
     *  End phase:
     *   - On Completed — quest resolved with an outcome. Outcome Tag tells you which (Victory / Defeat / etc.).
     *   - On Deactivated — quest was interrupted before completing.
     *   - On Blocked — Blocked-state fact transitioned absent → present (SetBlocked utility node fired).
     *   - On Unblocked — Blocked-state fact transitioned present → absent (ClearBlocked utility node fired).
     *
     * Subscribe at any tag — pass a leaf to watch one quest, or a parent like SimpleQuest.Questline.MyLine to
     * receive events from every descendant under it. With LinkedQuestline graphs you can also subscribe at
     * any of an inlined node's perspectives (its standalone form OR any inlining context's form). Quest Tag
     * output gives the canonical event identity (where the event originated); Matched Channel output gives
     * the address relative to what you subscribed to.
     *
     * Catch-up: if the quest already reached one of these states before you subscribed, the matching pin
     * fires immediately on bind. Late binders aren't left waiting on events that already happened.
     *
     * Context output carries the full event payload — Triggered Actor, Instigator, Node Info, Custom Data —
     * so you don't need a separate lookup for who triggered the event or what payload came with it.
     *
     * The subscription persists until you call Cancel on the returned node or the Game Instance tears down.
     * Typical pattern: bind in Begin Play, wire pins, optionally call Cancel from End Play for per-actor lifetime.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Events",
        meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject",
                HidePin = "WorldContextObject,ExposedEvents", DefaultToSelf = "WorldContextObject",
                DisplayName = "Bind To Quest Event"))
    static UQuestEventSubscription* BindToQuestEvent(UObject* WorldContextObject, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag, UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/SimpleQuest.EQuestEventTypes")) int32 ExposedEvents = 255);

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
        if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag)) return FDelegateHandle();
        if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
        {
            return Signals->SubscribeMessage<TEvent>(QuestTag, Listener, Callback);
        }
        return FDelegateHandle();
    }
    
    /** Companion unbind: pairs with SubscribeToQuestEvent's returned handle. Safe no-op if the handle is invalid. */
    static void UnsubscribeFromQuestEvent(UObject* WorldContextObject, const FGameplayTag& QuestTag, FDelegateHandle Handle);

    // -------------------------------------------------------------------------------------------------------------
    // Quest state queries: read directly from WorldState and/or QuestState
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest.Questline.State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestLive(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest.Questline.State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestCompleted(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest.Questline.State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestPendingGiver(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest.Questline.State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestResolvedWith(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag, UPARAM(meta = (Categories = "SimpleQuest.Outcome"))FGameplayTag OutcomeTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest.Questline.State", meta = (WorldContext = "WorldContext"))
    static int32 GetQuestCompletionCount(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    // -------------------------------------------------------------------------------------------------------------
    // Quest actions: publish to the signal bus; designer never touches the bus
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest.Questline.Actions", meta = (WorldContext = "WorldContext"))
    static void DeactivateQuest(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest.Questline.Actions", meta = (WorldContext = "WorldContext"))
    static void GiveQuest(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest.Questline.Actions", meta = (WorldContext = "WorldContext"))
    static void ActivateQuest(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest.Questline.Actions", meta = (WorldContext = "WorldContext"))
    static void SetQuestBlocked(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest.Questline.Actions", meta = (WorldContext = "WorldContext"))
    static void ClearQuestBlocked(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest.Questline.Actions", meta = (WorldContext = "WorldContext"))
    static void ResolveQuest(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag, UPARAM(meta = (Categories = "SimpleQuest.Outcome"))FGameplayTag OutcomeTag, bool bOverrideExisting = false);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest.Questline.Actions", meta = (WorldContext = "WorldContext"))
    static void StartQuestline(const UObject* WorldContext, TSoftObjectPtr<UQuestlineGraph> QuestlineGraph);

private:
    static UWorldStateSubsystem* GetWorldState(const UObject* WorldContext);
    static USignalSubsystem* GetSignalSubsystem(const UObject* WorldContext);
    static UQuestManagerSubsystem* GetQuestManager(const UObject* WorldContext);
    
};
