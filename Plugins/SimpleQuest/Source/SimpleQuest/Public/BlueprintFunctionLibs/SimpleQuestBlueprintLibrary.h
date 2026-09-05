// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Events/QuestEventBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Quests/Types/QuestEventPayload.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "Quests/Types/QuestRewardPreview.h"
#include "Quests/Types/QuestRoleSourceInfo.h"
#include "Quests/Types/SimpleQuestSaveSnapshot.h"
#include "Settings/SimpleQuestSettings.h"
#include "Subsystems/SignalSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "SimpleQuestBlueprintLibrary.generated.h"

class UQuestDisplayData;
class UQuestStateSubsystem;
class UQuestObjective;
class UQuestLifecycleObserver;
class UQuestlineGraph;
class UQuestManagerSubsystem;
class UWorldStateSubsystem;

/**
 * Severity for LogSimpleQuestMessage - the levels a Blueprint actually wants to emit at. A deliberately small subset
 * of ELogVerbosity: no Off (a non-logging "log" call is meaningless) and no Fatal (a Blueprint shouldn't be able to
 * assert the game down). For configuring per-category log thresholds, see EQuestLogVerbosity in SimpleQuestSettings.
 */
UENUM(BlueprintType)
enum class EQuestLogLevel : uint8
{
    Error   UMETA(DisplayName = "Error"),
    Warning UMETA(DisplayName = "Warning"),
    Display UMETA(DisplayName = "Display"),
    Verbose UMETA(DisplayName = "Verbose"),
};

UCLASS()
class SIMPLEQUEST_API USimpleQuestBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    
public:
    
    /**
     * Subscribe to a quest's lifecycle events. Configure which exec pins to expose via the checkboxes in
     * the Details panel - defaults to On Enabled / On Started / On Completed; opt in to others as needed.
     *
     * Available events organized by phase:
     *
     *  Offer phase:
     *   - On Activated - quest reached a giver-gated waypoint. Prereq Status says whether prereqs are met.
     *   - On Enabled - quest became accept-ready (Activated AND prereqs satisfy).
     *   - On Disabled - accept-ready quest became no-longer-ready (NOT-prereq edge cases; rare).
     *   - On Give Blocked - a give attempt was refused. Blockers carries the structured reasons.
     *   - On Activation Failed - an activation attempt was refused. Reason says why; Attempted Tag Name is
     *     populated even when Quest Tag is empty, which is the stale-tag Unknown Quest case.
     *
     *  Run phase:
     *   - On Started - quest entered Live state; objectives are bound and ticking.
     *   - On Progress - objective progress tick.
     *   - On Progress Refused - a trigger fired at a Live quest whose gate isn't open. Run-phase partner to
     *     On Give Blocked, and it shares the same Blockers pin.
     *
     *  End phase:
     *   - On Completed - quest resolved with an outcome. Outcome Tag tells you which (Victory / Defeat / etc.).
     *   - On Deactivated - quest was interrupted before completing.
     *   - On Blocked - Blocked-state fact transitioned absent → present (SetBlocked utility node fired).
     *   - On Unblocked - Blocked-state fact transitioned present → absent (ClearBlocked utility node fired).
     *
     * Subscribe at any tag - pass a leaf to watch one quest, or a parent like SimpleQuest.Questline.MyLine to
     * receive events from every descendant under it. With LinkedQuestline graphs you can also subscribe at
     * any of an inlined node's perspectives (its standalone form OR any inlining context's form). Quest Tag
     * output gives the canonical event identity (where the event originated); Matched Channel output gives
     * the address relative to what you subscribed to.
     *
     * Catch-up: if the quest already reached one of these STATES before you subscribed, the matching pin fires
     * immediately on bind, so late binders aren't left waiting on something that already happened. This covers
     * On Activated, On Enabled, On Started, On Completed, On Deactivated and On Blocked - the events backed by
     * a state fact that can be read back. The rest are transient: On Disabled, On Give Blocked, On Activation
     * Failed, On Progress, On Progress Refused, and On Unblocked describe a moment rather than a state, so there
     * is nothing to replay and they only ever fire live.
     *
     * Context output carries the full event payload - Triggered Actor, Instigator, Node Info, Custom Data -
     * so you don't need a separate lookup for who triggered the event or what payload came with it.
     *
     * The subscription persists until you call Cancel on the returned node or the Game Instance tears down.
     * Typical pattern: bind in Begin Play, wire pins, optionally call Cancel from End Play for per-actor lifetime.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Events",
    meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject",
            HidePin = "WorldContextObject,ExposedEvents", DefaultToSelf = "WorldContextObject",
            DisplayName = "Observe Quest Lifecycle"))
    static UQuestLifecycleObserver* ObserveQuestLifecycle(
        UObject* WorldContextObject,
        UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QuestTag,        
        UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/SimpleQuest.EQuestEventTypes")) int32 ExposedEvents = 0,
        ESignalRoutingMode Routing = ESignalRoutingMode::Descendants);

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

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestLive(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestCompleted(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestPendingGiver(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    /** True when any hold currently reaches QuestTag, whether placed on it or on a container above it. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestAdvancementHeld(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    /** Reasons for every hold reaching QuestTag. For UI, and for finding out why a pause will not end. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|State", meta = (WorldContext = "WorldContext"))
    static TArray<FName> GetActiveHoldReasons(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestBlocked(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestResolvedWith(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag, UPARAM(meta = (Categories = "SimpleQuest.Outcome"))FGameplayTag OutcomeTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|State", meta = (WorldContext = "WorldContext"))
    static int32 GetQuestCompletionCount(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag);

    // -------------------------------------------------------------------------------------------------------------
    // Source registry queries - find "which Giver / Trigger / Observer in the world handles this?" without maintaining a
    // parallel tag → actor registry. Queries alias-walk via the existing QuestStateSubsystem canonical-resolution
    // infrastructure - a result surfaces regardless of whether the query was authored against the canonical or an alias form.
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Source", meta = (WorldContext = "WorldContext"))
    static TArray<FQuestRoleSourceInfo> GetActiveTriggersForTag(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QueryTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Source", meta = (WorldContext = "WorldContext"))
    static TArray<FQuestRoleSourceInfo> GetActiveGiversForTag(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QueryTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Source", meta = (WorldContext = "WorldContext"))
    static TArray<FQuestRoleSourceInfo> GetActiveObserversForTag(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QueryTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Source", meta = (WorldContext = "WorldContext"))
    static UQuestObjective* GetActiveObjectiveForTag(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QueryTag);
    
    // -------------------------------------------------------------------------------------------------------------
    // Quest Rewards
    // -------------------------------------------------------------------------------------------------------------

    /**
     * The rewards a completing node advertises regardless of how it resolves - the any-outcome bucket only. The giver /
     * journal case: "complete this, get these." Computes each reward's preview live (pass the viewing actor); querying
     * never grants.
     *
     * Excludes outcome-specific rewards. See also: Get Advertised Rewards For Outcome, for rewards associated with
     * specific outcomes.
     *
     * @param WorldContext        world context for resolving the quest system
     * @param ContentTag          the Step or container whose advertised rewards you want
     * @param Viewer              viewing actor for computing live values
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Rewards", meta = (WorldContext = "WorldContext", DeprecatedFunction,
        DeprecationMessage = "Use Get Advertised Rewards and read the SimpleQuest.Outcome.AnyOutcome key. Removed in 0.9."))
    static TArray<FQuestRewardPreview> GetAdvertisedRewardsForAnyOutcome(const UObject* WorldContext, FGameplayTag ContentTag, AActor* Viewer);

    /**
     * Cold twin of GetAdvertisedRewards - everything a tag pays, both channels, read off a questline ASSET with no
     * running game. For catalog UI (quest-giver hub, bounty board) showing what a quest pays before it is accepted.
     * Viewer-dependent rewards compute off the compiled template; a cold catalog may pass null.
     *
     * *** RETURN TYPE CHANGED IN 0.8.1 *** from a flat array to the same outcome-keyed map the live query returns.
     * Blueprints calling the old form fail on a pin type mismatch rather than quietly receiving a different shape.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Rewards")
    static TMap<FGameplayTag, FQuestRewardPreviewList> GetAdvertisedRewardsFromAsset(const UQuestlineGraph* Questline, FGameplayTag Tag, AActor* Viewer);

    /**
     * Cold query for a questline's QUESTLINE-LEVEL rewards - what completing the whole questline pays, per outcome, read
     * directly off the asset's authored QuestlineRewards map. Manager-free / works pre-activation (the catalog / bounty-
     * board case). Distinct from GetAdvertisedRewardsFromAsset, which surfaces rewards wired into content NODES; this is
     * the questline's own completion reward, keyed by its top-level Exit outcome. Each reward is previewed via
     * DescribeReward (pass the viewing actor for live-computed values).
     *
     * @return outcome tag -> the previews that outcome pays. Empty map for a questline with no questline-level rewards.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Rewards", meta = (DeprecatedFunction,
        DeprecationMessage = "Use Get Advertised Rewards From Asset - it accepts a questline tag and also folds in node rewards. Removed in 0.9."))
    static TMap<FGameplayTag, FQuestRewardPreviewList> GetQuestlineRewardsFromAsset(const UQuestlineGraph* Questline, AActor* Viewer);

    /**
     * Live query for a RUNNING questline's questline-level rewards, per outcome - what completing this active questline
     * will pay. HUD/journal companion to the cold GetQuestlineRewardsFromAsset. Returns empty if the questline isn't
     * currently loaded (use the cold asset query for pre-activation catalogs).
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Rewards", meta = (WorldContext = "WorldContext", DeprecatedFunction,
        DeprecationMessage = "Use Get Advertised Rewards - it accepts a questline tag and also folds in node rewards. Removed in 0.9."))
    static TMap<FGameplayTag, FQuestRewardPreviewList> GetQuestlineRewards(const UObject* WorldContext, FGameplayTag QuestlineTag, AActor* Viewer);

    /**
     * Everything a tag pays on completion, keyed by outcome - rewards wired into the graph AND the questline's own
     * completion rewards, in one answer. Pass a Step, a container, a linked placement, or a questline identity; the
     * framework resolves which channels apply.
     *
     * SimpleQuest.Outcome.AnyOutcome is a key in its own right, NOT duplicated into the named outcomes: completing with
     * outcome X pays X's list PLUS the Any-Outcome list, which is exactly how delivery grants them. Summing one
     * outcome's list with the Any-Outcome list is the total for that completion; summing the whole map is not a total
     * anyone receives.
     *
     * Each preview carries SourceTag, so a merged list can still be traced back to what pays it.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Rewards", meta = (WorldContext = "WorldContext"))
    static TMap<FGameplayTag, FQuestRewardPreviewList> GetAdvertisedRewards(const UObject* WorldContext, FGameplayTag Tag, AActor* Viewer);
    
    /**
     * The rewards a completing node advertises for a specific outcome - the rewards on that outcome's path, plus (unless
     * bIncludeAnyOutcome is false) the any-outcome rewards, since the any-outcome route fires on every completion.
     * Computes each preview live; querying never grants.
     * 
     * @param WorldContext      world context for resolving the quest system
     * @param ContentTag        the Step or container whose advertised rewards you want
     * @param OutcomeTag        the outcome to preview (a registered outcome tag; static-outcome paths only)
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Rewards", meta = (WorldContext = "WorldContext"))
    static TArray<FQuestRewardPreview> GetAdvertisedRewardsForOutcome(const UObject* WorldContext, FGameplayTag ContentTag, FGameplayTag OutcomeTag, AActor* Viewer, bool bIncludeAnyOutcome = true);

    /**
     * Every outcome of a completing content node and what each pays, as a map - the "whole picture" companion to
     * GetAdvertisedRewardsForOutcome (which asks one outcome at a time). For a journal/tooltip showing "Success: X,
     * Failure: Y". Each outcome's list includes the any-outcome rewards (they fire regardless). Static outcomes only;
     * dynamic paths aren't represented (they have no author-time outcome tag).
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|Rewards", meta = (WorldContext = "WorldContext", DeprecatedFunction,
        DeprecationMessage = "Use Get Advertised Rewards - same map, and it also folds in questline-level rewards. Removed in 0.9."))
    static TMap<FGameplayTag, FQuestRewardPreviewList> GetAllAdvertisedRewardsByOutcome(const UObject* WorldContext, FGameplayTag ContentTag, AActor* Viewer);
    
    // -------------------------------------------------------------------------------------------------------------
    // Display data queries - lookup the display data associated with a given quest tag.
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Display", meta = (WorldContext = "WorldContext"))
    static FText GetQuestDisplayNameText(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QueryTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Display", meta = (WorldContext = "WorldContext"))
    static FText GetQuestDescriptionText(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QueryTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Display", meta = (WorldContext = "WorldContext"))
    static UQuestDisplayData* GetQuestDisplayDataAsset(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QueryTag);
    
    // -------------------------------------------------------------------------------------------------------------
    // Quest actions: publish to the signal bus; designer never touches the bus
    //
    // Each action accepts an optional payload - Context (FQuestEventPayload) for lifecycle-control ops carries
    // attribution data (Instigator / CustomData / OriginTag / OriginChain) that the manager threads through into
    // the resulting lifecycle event's Payload field; Params (FQuestObjectiveActivationParams) for activation-side
    // ops carries Authored override + Dynamic context stamped onto the destination Step's activation. BP pins are
    // optional via AutoCreateRefTerm; callers that don't supply one publish with an empty payload.
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext", AutoCreateRefTerm = "Payload"))
    static void DeactivateQuest(const UObject* WorldContext,
        UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QuestTag,
        const FQuestEventPayload& Payload = FQuestEventPayload());

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext", AutoCreateRefTerm = "Params"))
    static void GiveQuest(const UObject* WorldContext,
        UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QuestTag,
        const FQuestObjectiveActivationParams& Params = FQuestObjectiveActivationParams());

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext", AutoCreateRefTerm = "Params"))
    static void ActivateQuest(const UObject* WorldContext,
        UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QuestTag,
        const FQuestObjectiveActivationParams& Params = FQuestObjectiveActivationParams(),
        bool bBypassPrerequisites = false);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext", AutoCreateRefTerm = "Payload"))
    static void SetQuestBlocked(const UObject* WorldContext,
        UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QuestTag,
        const FQuestEventPayload& Payload = FQuestEventPayload(),
        bool bAlsoDeactivate = false);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext", AutoCreateRefTerm = "Payload"))
    static void ClearQuestBlocked(const UObject* WorldContext,
        UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QuestTag,
        const FQuestEventPayload& Payload = FQuestEventPayload());

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext"))
    static void ResetQuestRunState(const UObject* WorldContext,
        UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext", AutoCreateRefTerm = "Payload"))
    static void ResolveQuest(const UObject* WorldContext,
        UPARAM(meta = (Categories = "SimpleQuest.Questline")) FGameplayTag QuestTag,
        UPARAM(meta = (Categories = "SimpleQuest.Outcome")) FGameplayTag OutcomeTag,
        bool bOverrideExisting = false,
        const FQuestEventPayload& Payload = FQuestEventPayload());

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext", AutoCreateRefTerm = "Params"))
    static void StartQuestline(const UObject* WorldContext, TSoftObjectPtr<UQuestlineGraph> QuestlineGraph,
        const FQuestObjectiveActivationParams& Params = FQuestObjectiveActivationParams());
    
    /**
     * Restore a questline from a loaded save. Call AFTER Apply Snapshot (on the Quest State subsystem) has restored the
     * quest facts and history - this rebuilds the objectives the save recorded as in-progress, without re-running the
     * questline from its start. Safe to call on every questline graph in your game on load; graphs that weren't in
     * progress simply stay dormant. The load-time counterpart to Start Questline.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Actions", meta = (WorldContext = "WorldContext"))
    static void RestoreQuestline(const UObject* WorldContext, TSoftObjectPtr<UQuestlineGraph> QuestlineGraph);

    /**
     * Capture all SimpleQuest runtime state into a snapshot ready to embed in your save. Records the quest facts,
     * history, AND which questline graphs are in play - so Restore Quest State can rebuild everything without you
     * listing assets. Embed the returned struct in your USaveGame and write it with Save Game to Slot.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Save Load", meta = (WorldContext = "WorldContext"))
    static FSimpleQuestSaveSnapshot CaptureQuestState(const UObject* WorldContext);

    /**
     * Restore SimpleQuest from a snapshot loaded out of your save. Applies the saved facts + history, then rebuilds the
     * in-progress objectives for every questline graph the snapshot recorded - no need to know which assets were active.
     * Call on load (after Load Game from Slot); it replaces starting your questlines fresh.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Save Load", meta = (WorldContext = "WorldContext"))
    static void RestoreQuestState(const UObject* WorldContext, const FSimpleQuestSaveSnapshot& Snapshot);

    /**
     * Restore the quest DATA from a snapshot (facts + history) and remember which graphs to rebuild. Call on load BEFORE
     * you open your gameplay level, so the level's actors register against restored data and re-sync for free. Then either
     * let bRestoreOnNextLevelLoad rebuild the questlines automatically when your level opens, or call Restore Quest Graphs
     * yourself once it's up. (For an in-place quick-load with no level change, use Restore Quest State instead.)
     *
     * @param Snapshot                  The data to apply to the World State and Quest State Subsystems: typically an
     *                                  FSimpleQuestSaveSnapshot struct embedded in a custom save game object.
     * @param bRestoreOnNextLevelLoad   When true, the questlines rebuild automatically the moment your next level finishes
     *                                  loading - no Restore Quest Graphs node needed anywhere. Leave false to drive it yourself.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Save Load", meta = (WorldContext = "WorldContext"))
    static void ApplyQuestSnapshot(const UObject* WorldContext, const FSimpleQuestSaveSnapshot& Snapshot, bool bRestoreOnNextLevelLoad = false);

    /**
     * Rebuild the in-progress questlines remembered by the most recent Apply Quest Snapshot - call this in your gameplay
     * level's startup, in the same place a new game would start its questlines. Takes no arguments: the save already
     * recorded which graphs were active, so you never enumerate them.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Save Load", meta = (WorldContext = "WorldContext"))
    static void RestoreQuestGraphs(const UObject* WorldContext);
    
    /**
     * Pauses quest advancement under QuestTag until every hold on it is released. Holding a container holds everything
     * inside it; holding a questline's identity tag holds the whole questline.
     *
     * CALL THIS FROM THE COMPLETION EVENT ITSELF, not from something the completion event schedules. The hold has to
     * exist before the cascade activates the next node, and both happen in the same synchronous call. A Delay, a
     * timer, or an async callback all return control first - by then the next node is already live.
     * The usual shape is: hold on the completion event, start the audio, release in the audio's finished callback.
     * The hold is what makes that second yield safe.
     *
     * SERVER-SIDE. In a networked game a client sees the completion only after the server has already advanced.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Advancement", meta = (WorldContext = "WorldContext"))
    static FQuestAdvancementHold HoldQuestAdvancement(const UObject* WorldContext, UPARAM(meta = (Categories = "SimpleQuest.Questline"))FGameplayTag QuestTag, FName Reason, bool bHoldDeactivation = true);

    /** Releases one hold. Advancement resumes only when the last hold on a tag clears. Releasing twice is harmless. */
    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Advancement", meta = (WorldContext = "WorldContext"))
    static void ReleaseQuestAdvancement(const UObject* WorldContext, const FQuestAdvancementHold& Hold);

    // -------------------------------------------------------------------------------------------------------------
    // Logging - write into SimpleQuest's "Module" log category from Blueprints at a chosen verbosity. Print String is
    // fixed at Display; this lets BP diagnostics surface as Warning/Error and honor the per-category verbosity set in
    // Project Settings → Simple Quest → Logging.
    // -------------------------------------------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Debug", meta = (DisplayName = "Log Simple Quest Message"))
    static void LogSimpleQuestMessage(const FString& Message, EQuestLogLevel Level = EQuestLogLevel::Warning);

private:
    static UWorldStateSubsystem* GetWorldStateSubsystem(const UObject* WorldContext);
    static USignalSubsystem* GetSignalSubsystem(const UObject* WorldContext);
    static UQuestManagerSubsystem* GetQuestManagerSubsystem(const UObject* WorldContext);
    static UQuestStateSubsystem* GetQuestStateSubsystem(const UObject* WorldContext);    
};
