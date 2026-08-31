// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include <concepts>
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Engine/AssetManager.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/OriginatingEventID.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "Quests/Types/QuestObjectiveRuntimeContext.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "Quests/Types/QuestRewardPreview.h"
#include "Quests/Types/PrereqLeafSubscription.h"
#include "Quests/Types/QuestAdvancementHold.h"
#include "Quests/Types/SimpleQuestObjectiveSaveState.h"
#include "QuestManagerSubsystem.generated.h"


struct FQuestGiveBlockedEvent;
struct FQuestProgressRefusedEvent;
struct FQuestGraphResolution;
struct FQuestActivationRequestEvent;
struct FQuestBlockRequestEvent;
struct FQuestBoundaryCompletion;
struct FQuestClearBlockRequestEvent;
struct FQuestDeactivateRequestEvent;
struct FQuestDeactivatedEvent;
struct FQuestEntryRecordedEvent;
struct FQuestEventPayload;
struct FQuestGiverRegisteredEvent;
struct FQuestGivenEvent;
struct FQuestlineStartRequestEvent;
struct FQuestResolutionRecordedEvent;
struct FQuestResolveRequestEvent;
struct FWorldStateFactAddedEvent;
struct FWorldStateFactRemovedEvent;

struct FInstancedStruct;

enum class EDeactivationSource : uint8;
enum class EQuestActivationProvenance : uint8;
enum class EQuestStateLeaf : uint8;

class UQuestlineGraph;
class UQuestNodeBase;
class UQuestStateSubsystem;
class UQuestStep;
class USignalSubsystem;
class UWorldStateSubsystem;


/**
 * UObject-derived constraint for the soft-loading helpers below. TSoftObjectPtr<T> and TSoftClassPtr<T>
 * already require T to derive from UObject under the hood, but surfacing that as a concept gives clean
 * compile errors at the caller rather than cascading failures inside the template body.
 */
template<typename T>
concept CSoftLoadable = std::derived_from<T, UObject>;

/**
 * Opaque orchestration subsystem for the SimpleQuest runtime. Owns the activation cascade, the lifecycle state machine,
 * the giver-component registry, and the event-handler graph that ties them together. Friend-only writer to
 * UQuestStateSubsystem - the manager IS the writer, QSS is the reader. Friend-only consumer of the SimpleCore
 * subsystems (USignalSubsystem for publish/subscribe routing, UWorldStateSubsystem for boolean state facts) - the
 * manager pushes facts and publishes events as quest lifecycle transitions fire.
 *
 * ARCHITECTURAL CONTRACT - adopters do NOT reach into this subsystem directly. The public BP-callable surface lives on
 * USimpleQuestBlueprintLibrary (request-side: ActivateQuestlineGraph, BlockQuest, DeactivateQuest, GiveQuest, etc.)
 * and on UQuestStateSubsystem (read-side: GetQuestResolution, QueryQuestActivationBlockers, GetDisplayName, etc.).
 * The manager's own surface is intentionally internal - orchestration logic is not part of the adopter contract, and
 * exposing it would couple adopter code to implementation details the framework reserves the right to evolve.
 *
 * Internal responsibilities:
 *   - Quest lifecycle orchestration: PendingGiver → Live → Resolved (or Deactivated), with prereq gating, multi-tag
 *     cascade deduplication, and container Live derivation from inner-Step state.
 *   - Activation cascade routing: walks each registered graph's compiled NextNodesByPath / NextNodesOnAnyOutcome tables
 *     to deliver outcomes from completing nodes to their downstream destinations.
 *   - Boundary completion firing: emits wrapper-level resolutions when an inner-graph outcome crosses a LinkedQuestline
 *     boundary so consumers subscribed at the wrapper see the resolution at the wrapper's perspective.
 *   - Loaded-instance registry (LoadedNodeInstances): maps every registered tag perspective (canonical +
 *     AssetScopedAliasTag) to its runtime UQuestNodeBase, with deduplication so one logical authored node produces
 *     one runtime instance even when compiled into multiple assets via LinkedQuestline.
 *   - Giver-component registration: actors with UQuestGiverComponent register on BeginPlay, match against compiled
 *     quest tags, and have their availability re-evaluated as prereq state changes.
 *   - Async-load orchestration: reachability-walks loaded graphs' OutwardSetterGroupTags against the project-wide
 *     listener-graph index to async-load listener-bearing graphs on demand rather than at startup.
 *   - Fact + event push to SimpleCore: WorldState boolean facts (Started / Live / Resolved / Deactivated) and bus
 *     events (FQuestStartedEvent, FQuestEndedEvent, FQuestActivationFailedEvent, etc.) for every registered perspective
 *     so any-perspective queries and subscribers see consistent state.
 *   - Pushes display data into UQuestStateSubsystem's registry at graph-registration time so adopter UI queries
 *     (GetDisplayName / GetDisplayDescription / GetDisplayData) resolve without crossing the manager boundary.
 *
 * Naming convention mirrored across SimpleCore + SimpleQuest: "Manager Subsystem" denotes opaque orchestration with
 * limited public surface; "State Subsystem" denotes a public query registry the manager writes to via friend access.
 * See UQuestStateSubsystem for the read-side counterpart and the two-layer state architecture rationale.
 */
UCLASS(Blueprintable, config = Game)
class SIMPLEQUEST_API UQuestManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	friend class USimpleQuestBlueprintLibrary;
	
	/**
	 * PIE debug overlay. Maps a resolved runtime tag back to its registered instance to evaluate live prerequisite state.
	 * Reaches in rather than the manager growing public API for one consumer, matching the Blueprint library above.
	 */
	friend class FQuestPIEDebugChannel;
	
	/**
	 * Advancement-hold tests. Reaches in for the same reason the debug channel does: the hold API is protected because
	 * it shares a class with the replacement-orchestrator variation points, not because holds are internal.
	 */
	friend class FQuestAdvancementHoldTestAccess;

	/** Deferred activations ask whether a hold applies before going live. */
	friend class UQuestNodeBase;
	
protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * Registers a graph's compiled node instances into LoadedNodeInstances WITHOUT firing entry nodes.
	 * Wires CachedGameInstance, OnRegisteredWithManager (so e.g. UActivationGroupListenerNode can subscribe
	 * to its group signal channel at instance lifetime), the per-node delegate binds, the per-tag deactivation
	 * subscription, and the container-classification push to UQuestStateSubsystem. Used by both
	 * ActivateQuestlineGraph (which calls this then fires entry tags) and the startup autoload path for
	 * listener-bearing graphs picked up via HasActivationGroupListener asset registry scan. Not idempotent on
	 * the same graph - re-binding delegates and double-subscribing deactivation handlers; callers must deduplicate.
	 */
	virtual void RegisterQuestlineGraph(UQuestlineGraph* Graph);
	
	/** Registers all compiled node instances from the graph into LoadedNodeInstances and activates its entry nodes. */
	virtual void ActivateQuestlineGraph(UQuestlineGraph* Graph, const FQuestObjectiveActivationContext& Params = FQuestObjectiveActivationContext());

	/**
	 * Load-time counterpart to ActivateQuestlineGraph. Registers the graph's compiled instances, then - instead of
	 * firing entry nodes - reconstitutes the live objective on every Step the restored WorldState marks Live, replaying
	 * it from the saved entry snapshot with EQuestActivationProvenance::Restored. No lifecycle events, no entry records,
	 * no forward cascades: a loaded game rebuilds the exact Live set the save described rather than re-running the graph
	 * from its entries. Call after UQuestStateSubsystem::ApplySnapshot has restored facts + registries. Safe to call on
	 * every questline graph - graphs the save left dormant simply register and instantiate nothing. Designer-facing
	 * counterpart: USimpleQuestBlueprintLibrary::RestoreQuestline.
	 */
	virtual void RestoreQuestlineGraph(UQuestlineGraph* Graph);

	/**
	 * Restores every graph in the pending-restore stash (set by ApplyQuestSnapshot): async-loads each and rebuilds its
	 * live / deferred / accumulator state. Drives both the manual RestoreQuestGraphs BP node and the auto-consume flush.
	 */
	virtual void RestorePendingGraphs();

	/**
	 * Arms a one-shot auto-restore: the pending stash flushes automatically when the next game world initializes (i.e.
	 * after the consumer's OpenLevel), so the load path is just ApplyQuestSnapshot -> OpenLevel with no per-level node.
	 * Set by ApplyQuestSnapshot's bRestoreOnNextLevelLoad. Idempotent; cleared on flush or Deinitialize.
	 */
	virtual void ArmRestoreOnNextLevelLoad();

	/**
	 * Looks up the instance for NodeTagName in LoadedNodeInstances and activates it. Stamps Provenance onto the
	 * destination's PendingActivationContext so it rides through ActivateInternal's merge into ReceivedActivationContext;
	 * HandleOnNodeStarted then captures it on the FQuestEntryArrival snapshot the state subsystem persists, giving
	 * catch-up subscribers and save/load reconstitution access to "how was this activation initiated?"
	 *
	 * Provenance is required at every call site by design. A defaulted Unknown value would propagate silently into
	 * the historical record; making the value explicit forces a deliberate decision at every entry point. Forgotten
	 * call sites become compile errors instead of inscrutable Unknown stamps in the registry.
	 *
	 * @param NodeTagName         FName key into LoadedNodeInstances. Resolved to FGameplayTag via UGameplayTagsManager
	 *                            for runtime operations. Logs a warning and no-ops if no instance is loaded under
	 *                            this key.
	 * @param Provenance          How this activation was initiated. InitialEntry for graph entry-tag fires at startup;
	 *                            GiverGate for HandleGiveQuestEvent's give-completion re-activation; ChainCascade for
	 *                            outcome / forward / deactivation routing from another node; ExternalAPI for
	 *                            FQuestActivationRequestEvent and programmatic / procedural / save-rehydration paths.
	 * @param IncomingOutcomeTag  Outcome from the parent node for per-path entry routing in UQuest container nodes.
     *                            Stamped onto PendingActivationContext.IncomingOutcomeTag and consumed by the wrapper's
	 *                            inner-entry routing. Invalid (default) for non-cascade activations.
	 * @param IncomingSourceTag   FName of the specific parent source whose outcome fired. UQuest entry routing filters
	 *                            source-qualified entries against this tag so only the matching spec's entry step
	 *                            fires - enables per-source routing on duplicate paths. NAME_None (default) for
	 *                            non-cascade activations.
	 * @param bBypassGiverGate    When true, routes past the giver gate to Live without entering PendingGiver. Set by
	 *                            HandleGiveQuestEvent's give-completion re-activation; the player has already accepted,
	 *                            so re-entering PendingGiver would be incorrect. The structural giver-gated set still
	 *                            contains the tag for the next loop iteration / external re-activation. Defaults to
	 *                            false for all non-give paths.
	 * @param bBypassPrerequisites
	 *
	 * @see UQuestStateSubsystem::RecordEntry
	 * @see UQuestManagerSubsystem::HandleOnNodeStarted
	 * @see FQuestObjectiveActivationContext::Provenance
	 */
	virtual void ActivateNodeByTag(
		FName NodeTagName,
		EQuestActivationProvenance Provenance,
		FGameplayTag IncomingOutcomeTag = FGameplayTag(),
		FName IncomingSourceTag = NAME_None,
		bool bBypassGiverGate = false,
		bool bBypassPrerequisites = false);

	/**
	 * Pauses quest advancement under QuestTag until every hold placed on it is released.
	 *
	 * THE FRAMEWORK OWNS SUSPEND AND RESUME. IT DOES NOT OWN PACING. There is no delay, timer, or per-node setting
	 * here on purpose - how long a pause lasts is a game decision, and a game that wants one already has timers.
	 *
	 * MATCHING IS EXACT-OR-ANCESTOR, ACROSS EVERY TAG PERSPECTIVE a node answers to. Tag ancestry mirrors containment,
	 * so holding any container holds everything inside it, and holding a questline's root tag holds the whole
	 * questline as the top case of that same rule.
	 *
	 * ONLY CASCADE ACTIVATIONS ARE HELD. A player accepting a quest from a giver still works while a hold is in force;
	 * pacing is about what follows a completion, not about refusing input.
	 *
	 * CALL THIS FROM SERVER-SIDE GAME CODE. A hold must exist before the cascade runs, and the cascade is
	 * authority-side. In a networked game a client observing a completion and holding is not merely racy - it is late
	 * every time, because the client's event derives from a fact that has already replicated, which means the server
	 * has already advanced. Client-driven pacing has to request the hold ahead of the transition.
	 *
	 * @param QuestTag           Content to pause. A node tag, a container tag, or a questline's identity tag.
	 * @param Reason             Free-form label. Surfaces in the log and in GetActiveHoldReasons; a stuck hold is
	 *                           otherwise undiagnosable, which is the whole reason this is required rather than optional.
	 * @param bHoldDeactivation  Whether deactivation chains pause too. Defaults true so a hold means "pause
	 *                           advancement" without qualification. Pass false when deactivation routes are corrective
	 *                           cleanup that should proceed while forward progress waits.
	 * @return                   Handle to pass to ReleaseQuestAdvancement. Invalid if QuestTag was invalid.
	 */
	virtual FQuestAdvancementHold HoldQuestAdvancement(FGameplayTag QuestTag, FName Reason, bool bHoldDeactivation = true);

	/**
	 * Releases one hold. Advancement resumes only when the LAST hold on a tag clears, so an audio hold and a cutscene
	 * hold compose without either knowing the other exists. Releasing an already-released handle is a no-op.
	 */
	virtual void ReleaseQuestAdvancement(const FQuestAdvancementHold& Hold);

	/** True when any hold currently reaches QuestTag, whether placed on it or on a container above it. */
	virtual bool IsQuestAdvancementHeld(FGameplayTag QuestTag) const;

	/** Reasons for every hold currently reaching QuestTag. For debug surfaces and for diagnosing a pause that will not end. */
	virtual TArray<FName> GetActiveHoldReasons(FGameplayTag QuestTag) const;

	/**
	 * Releases every hold and replays everything parked. Returns how many holds were dropped.
	 *
	 * Called before a save snapshot is captured, which is what keeps held state out of saves entirely - by the time
	 * facts are gathered the Held leaves are gone, so no transient-fact mechanism is needed. Saving mid-pause skips
	 * the rest of that pause, which is the intended trade: pacing does not survive a save/load boundary, and a save
	 * that restored a pause would restore a game that looks stuck.
	 */
	virtual int32 ReleaseAllQuestAdvancementHolds();
		
	/** Chains to next nodes after a node completes, using tag-based routing from NextNodesByPath / NextNodesOnAnyOutcome. */
	virtual void ChainToNextNodes(
		UQuestNodeBase* CompletedNode,
		FGameplayTag OutcomeTag,
		FName PathIdentity,
		const FOriginatingEventID& OriginatingEventID = FOriginatingEventID(),
		const FQuestObjectiveActivationContext& InheritedForward = FQuestObjectiveActivationContext());

	/**
	 * Resolve the rewards a completing node advertises on an outcome path (backs USimpleQuestBlueprintLibrary::
	 * GetAdvertisedRewardsForAnyOutcome and ...ForOutcome). Looks the node up by ContentTag, reads its compile-time
	 * ReachableRewardsByPath, resolves each reward-node key to its live instance, and aggregates DescribeReward. Pure:
	 * no grant, no event. PathIdentity is the compile-time path key (a static outcome's tag-name, a dynamic PathName,
	 * or NAME_None for any-outcome); resolved from the caller's outcome tag by the library.
	 */
	virtual TArray<FQuestRewardPreview> ResolveAdvertisedRewards(FGameplayTag ContentTag, FName PathIdentity, AActor* Viewer, bool bIncludeAnyOutcome) const;

	/**
	 * Live twin of the cold GetQuestlineRewardsFromAsset - previews a RUNNING questline's questline-level rewards per
	 * outcome. Resolves the identity tag to its live graph (LiveGraphsByIdentity, the same registry delivery uses) and
	 * describes each authored reward. For HUD/journal on an active questline. Backs USimpleQuestBlueprintLibrary.
	 */
	virtual TMap<FGameplayTag, FQuestRewardPreviewList> ResolveQuestlineRewards(FGameplayTag QuestlineTag, AActor* Viewer) const;
	
	/**
	 * Everything a tag pays on completion, keyed by the outcome that pays it - rewards wired into the graph AND the
	 * questline's own completion rewards, merged into one answer. Backs the Blueprint library's Get Advertised Rewards.
	 *
	 * TAKES WHATEVER TAG THE CALLER HAS: a Step, a container, a linked placement, or a questline identity. The node
	 * channel comes from that tag's own reachability manifest; the questline channel is delegated to
	 * ResolveQuestlineRewards, which resolves a placement's CONTEXTUAL tag to its inner asset IDENTITY through
	 * LinkedInnerIdentityTag. Choosing between those channels used to be the CALLER's job, and required knowing
	 * node-versus-asset and contextual-versus-identity in order to ask a single question.
	 *
	 * *** SimpleQuest.Outcome.AnyOutcome IS A KEY IN ITS OWN RIGHT, NOT FOLDED INTO THE NAMED OUTCOMES. *** Completing
	 * with outcome X pays X's list PLUS the Any-Outcome list, which is exactly how delivery grants them -
	 * PublishGraphResolutions looks the two up separately and grants both. So the union of one outcome with
	 * Any-Outcome is a real total, while the sum of the whole map is a number no completion ever pays.
	 *
	 * Every preview carries SourceTag (which completion it was resolved from) and RewardGuid (which reward produced
	 * it), so a merged list stays traceable and a UI that re-queries keeps its identities across refreshes.
	 *
	 * PURE: computes previews for the Viewer, grants nothing, publishes nothing. An outcome with no rewards has no key
	 * rather than an empty list; a tag with no loaded node yields an empty map, and the Verbose log says which.
	 */
	TMap<FGameplayTag, FQuestRewardPreviewList> ResolveAllRewards(FGameplayTag Tag, AActor* Viewer) const;

	/**
	 * Whole-node advertised rewards, grouped by outcome - every static-outcome path of a completing node and what each
	 * pays, PLUS the any-outcome rewards merged into each (any-outcome fires on every completion, so that's the truthful
	 * "complete via this outcome, get this" picture). Backs USimpleQuestBlueprintLibrary::GetAllAdvertisedRewardsByOutcome.
	 *
	 * BOUNDARY-APPROACH CAVEAT (0.6): the manifest keys on FName PathIdentity; this resolves PathIdentity -> outcome tag
	 * for STATIC outcomes only (RequestGameplayTag). Dynamic PathNames have no compile-time tag and are DROPPED (consistent
	 * with the dynamic-paths-not-BP-previewable boundary). The Path/Outcome un-fuse (0.7 opener) makes this exact + simpler.
	 */
	virtual TMap<FGameplayTag, FQuestRewardPreviewList> ResolveAllAdvertisedRewardsByOutcome(FGameplayTag ContentTag, AActor* Viewer) const;

	// ── Save / load + reset orchestration seam ──────────────────────────────────────────────────────────────────
	// The library (the manager's one friend + facade) drives these. They're the override points a replacement
	// orchestrator with different persistence/reset semantics re-implements. Protected virtual, not public - the
	// black box exposes no public API; only the friend library and subclasses reach them.

	/** Completion tally for a quest. Backs USimpleQuestBlueprintLibrary::GetQuestCompletionCount. */
	virtual int32 GetQuestCompletionCount(FGameplayTag QuestTag) const;

	/**
	 * The questline graphs the manager has registered or reachability-warmed this session, by soft path. Captured into
	 * the save snapshot (CaptureQuestState) so RestoreQuestState can drive graph restore off the save itself rather than
	 * a caller-maintained asset list. A superset of the graphs with live state - dormant entries restore to a no-op.
	 */
	virtual const TSet<FSoftObjectPath>& GetKnownLoadedGraphPaths() const { return KnownLoadedGraphPaths; }

	/** Stashes the snapshot's active-graph list + deferred-activation set + objective states for a level-transition restore. */
	virtual void StashPendingRestore(const TArray<FSoftObjectPath>& Graphs, const TMap<FGuid, FQuestObjectiveRuntimeContext>& Deferred,
		const TMap<FGuid, FSimpleQuestObjectiveSaveState>& ObjectiveStates)
	{
		PendingRestoreGraphs = Graphs;
		PendingDeferredActivations = Deferred;
		PendingObjectiveStates = ObjectiveStates;
	}

	/** The prereq-deferred activations currently armed on loaded nodes, keyed by contextual tag. Read by snapshot capture. */
	virtual TMap<FGuid, FQuestObjectiveRuntimeContext> CaptureDeferredActivations() const;

	/** Per-instance objective progress on live objectives, keyed by owning-Step QuestContentGuid. Read by snapshot capture. */
	virtual TMap<FGuid, FSimpleQuestObjectiveSaveState> CaptureObjectiveStates() const;

	/**
	 * The live node instance registered under Tag, or null when nothing is registered for it. Accepts any perspective the
	 * registry is keyed under: registration adds the canonical tag and every asset-scoped alias, all pointing at the same
	 * instance, so a caller does not need to resolve to canonical form first.
	 *
	 * Part of the replacement contract: an orchestrator that maintains its own registry instead of LoadedNodeInstances must
	 * override this, or editor tooling and any other introspection will silently report against a registry it no longer fills.
	 */
	virtual const UQuestNodeBase* FindNodeInstance(FGameplayTag Tag) const { return LoadedNodeInstances.FindRef(Tag.GetTagName()); }

	/** Clears the clearable state mirror for every path a quest has resolved through (append-only registry untouched). Backs ResetQuestRunState. */
	virtual void ResetQuestRunState(FGameplayTag QuestTag);

private:
	void LoadCompiledDisplayIni() const;
	
	void CheckQuestObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent);

	/** Returns and clears the stashed active-graph list. RestoreQuestGraphs drives per-graph restore from it. */
	TArray<FSoftObjectPath> ConsumePendingRestoreGraphs() { return MoveTemp(PendingRestoreGraphs); }

	void HandleWorldInitForRestore(UWorld* World, const UWorld::InitializationValues IVS);
	void DisarmRestoreOnNextLevelLoad();

	bool bRestoreArmed = false;
	FDelegateHandle PostWorldInitHandle;
	TWeakObjectPtr<UWorld> ArmedFromWorld;
	
	/** Node instances from all loaded questline graph assets, keyed by tag. Populated by ActivateQuestlineGraph. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UQuestNodeBase>> LoadedNodeInstances;

	/** One active advancement hold. Plain struct, not reflected - it holds no UObject references, so GC has no stake. */
	struct FQuestHoldRecord
	{
		FGameplayTag QuestTag;
		FName        Reason;
		bool         bHoldDeactivation = true;
		double       PlacedAtSeconds = 0.0;

		/** Latched once the abandonment warning has fired, so a stuck hold reports itself once rather than forever. */
		bool         bWarned = false;
	};

	/**
	 * An activation that arrived while a hold was in force, stored verbatim so releasing can re-enter the same funnel
	 * with the same arguments. Nothing was written to the destination node when this was captured, so a replayed
	 * activation is indistinguishable from one that was never held.
	 */
	struct FQuestParkedActivation
	{
		FName                      NodeTagName;
		EQuestActivationProvenance Provenance = EQuestActivationProvenance::Unknown;
		FGameplayTag               IncomingOutcomeTag;
		FName                      IncomingSourceTag;
		bool                       bBypassGiverGate = false;
		bool                       bBypassPrerequisites = false;
	};

	/** Active holds by id. Authority-side mechanism - never replicates; the Held FACT is what clients see. */
	TMap<int32, FQuestHoldRecord> ActiveHolds;

	/** Runs only while something is held; see CheckForAbandonedHolds, which starts nothing and stops itself. */
	FTimerHandle AbandonedHoldTimer;

	/** Timer entry. Reads the clock, delegates, and clears its own timer when nothing is held. */
	void CheckForAbandonedHolds();

	/**
	 * The comparison itself, taking the current time rather than fetching it. That is what lets a test hand it a time
	 * far past a hold's placement and call it directly, with no world to advance.
	 */
	void WarnOnHoldsOlderThan(double NowSeconds);

	/** Monotonic. Zero is never issued, so a default-constructed FQuestAdvancementHold can never match a live hold. */
	int32 NextHoldId = 1;

	/** Parked activations in arrival order. Replayed in that order when the last hold clears. */
	TArray<FQuestParkedActivation> ParkedActivations;

	/** True while ReplayParkedActivations is draining, so a re-entrant hold placed mid-replay cannot recurse. */
	bool bReplayingParkedActivations = false;

	/** Would this activation be held? Cascade provenances only; see the hold API for the matching rules. */
	bool ShouldHoldActivation(const UQuestNodeBase* Instance, FName NodeTagName, EQuestActivationProvenance Provenance, FName IncomingSourceTag) const;

	/** True when HoldTag equals, or is an ancestor of, any tag perspective this node answers to. */
	bool NodeMatchesHoldTag(const UQuestNodeBase* Instance, FName NodeTagName, FGameplayTag HoldTag) const;

	/** Re-enters ActivateNodeByTag for every parked entry, in arrival order. Clears the queue first so it cannot loop. */
	void ReplayParkedActivations();

	/** Tells every prerequisite-deferred node to re-evaluate. Called when a hold clears; see the function body. */
	void RetryDeferredActivations();

	/** Drops any hold whose subject just ended, warning per hold. Called from the completion path. */
	void ClearHoldsForEndedQuest(FGameplayTag QuestTag);

	/** Recomputes the Held fact for one tag from the surviving holds. Removes it when the last hold on that tag clears. */
	void RefreshHeldFact(FGameplayTag QuestTag);

	/**
	 * Live questlines by their identity tag (SimpleQuest.Questline.<EffectiveID>), populated in RegisterQuestlineGraph.
	 * The manager otherwise tracks only soft paths (KnownLoadedGraphPaths), not graph pointers - this is the one place
	 * it holds a graph ref, so questline-level reward delivery can resolve a resolution's GraphTag back to the asset and
	 * read its QuestlineRewards. Weak so an unloaded graph drops out without dangling.
	 */
	TMap<FGameplayTag, TWeakObjectPtr<UQuestlineGraph>> LiveGraphsByIdentity;

	/**
	 * Questline-level rewards by questline-asset identity tag name, flattened from every registered graph's
	 * CompiledQuestlineRewards (a graph contributes its own identity plus each linked questline inlined into it).
	 * Delivery (PublishGraphResolutions) and the reward queries read this by a resolution's GraphTag, so an embedded
	 * questline's rewards resolve without its source asset - which is never loaded at runtime. Populated in
	 * RegisterQuestlineGraph alongside LiveGraphsByIdentity.
	 */
	TMap<FName, FQuestCompiledQuestlineRewards> LiveQuestlineRewardsByIdentity;

	/**
	 * Resolves a perspective-form FGameplayTag to the canonical (Instance->GetContextualTag()) the runtime uses
	 * for state facts and registry entries. Layer 2 deduplication populates LoadedNodeInstances under the canonical key
	 * AND every alias key (all pointing at the same instance), so any-perspective input here resolves the same
	 * instance, and the instance's ContextualTag is always the canonical regardless of which key was used to
	 * look it up.
	 *
	 * Request-side BP APIs (DeactivateQuest, BlockQuest, ClearBlockedQuest, ResolveQuest) receive user-authored
	 * tags that may be any perspective. State facts are written and queried at the canonical, so handlers MUST
	 * resolve before acting - otherwise IsActiveLifecycle / IsBlocked / IsTerminal queries miss, and AddFact
	 * writes leak to alias-perspective fact tags that no query will ever read.
	 *
	 * Pass-through behavior: returns InputTag unchanged if not in LoadedNodeInstances (legacy, external,
	 * pre-registration, or unregistered tag). Callers must still handle any invalid-tag fallthrough.
	 */
	FGameplayTag ResolveToCanonicalTag(FGameplayTag InputTag) const;

	/**
	 * Resolves an adopter-supplied tag to the single canonical instance a mutation should target. Mutations are
	 * instance-specific: a contextual tag (or a class-channel alias with a single placement) resolves to that one
	 * instance; an alias shared by multiple placements is ambiguous - which instance? - and is refused with a
	 * Warning. Returns an invalid tag on refusal so callers fall through their existing invalid-tag guard.
	 */
	FGameplayTag ResolveSingleCanonicalForMutation(FGameplayTag InputTag) const;

	/**
	 * Adds (or removes) a state-leaf fact at the canonical perspective AND every AssetScopedAliasTag the
	 * instance carries, so direct WorldState->HasFact queries from any perspective find the fact. Mirrors
	 * the multichannel publish model the bus uses for events - facts and events both ride every
	 * perspective so consumers don't need to alias-walk at every read site.
	 *
	 * InputTag is canonicalized internally for safety; callers may pass any-perspective form. State-fact
	 * tags for every alias perspective are pre-registered by FSimpleQuest::RegisterCompiledQuestTags (the
	 * compile-time CompiledQuestTags list carries both contextual and alias FNames), so RequestGameplayTag
	 * resolves cleanly at each perspective.
	 *
	 * Add/Remove must stay symmetric - each Add bumps the ref-count by N (1 + alias count); the paired
	 * Remove decrements by N. The boolean idempotency guards in SetQuest* check canonical to prevent
	 * double-bumps under cascade convergence - they don't need per-perspective awareness because canonical
	 * is the source-of-truth for "is this state already set".
	 */
	void AddStateFactAcrossPerspectives(FGameplayTag InputTag, EQuestStateLeaf Leaf);
	void RemoveStateFactAcrossPerspectives(FGameplayTag InputTag, EQuestStateLeaf Leaf);
	
	/**
	 * Sets the append-only Started anchor for a node the first time it goes Live. Unlike the transient Live fact
	 * (re-derived away when a container's last active child finishes), this is never removed - it is the past-tense
	 * record catch-up reconstructs a node's Started/Activated from once Live has cleared and the node never itself
	 * resolved. Idempotent: written once, so the ref-count stays boolean. Called from every Live-add site.
	 */
	void MarkQuestStarted(FGameplayTag QuestTag);
	
	/**
	 * Writes the per-run resettable mirror - the MakeNodePathFact tag pin-wired prereqs carry - across the canonical
	 * tag and every AssetScopedAlias, matching AddStateFactAcrossPerspectives' multi-perspective fan. Called on
	 * resolution for resettable-replay-scoped nodes only; the resolution registry stays the permanent record. A
	 * None PathIdentity is a no-op (non-path resolutions don't project a mirror). The originating event identity is
	 * recorded against each written mirror tag so a node woken by the fact can recover which resolution wrote it.
	 */
	void AddPathFactAcrossPerspectives(FGameplayTag InputTag, FName PathIdentity, const FOriginatingEventID& OriginatingEventID);
	
	/**
	 * Clears the per-run path mirror (the MakeNodePathFact tag) across the canonical tag and every AssetScopedAlias -
	 * the ClearFact (count-agnostic) twin of AddPathFactAcrossPerspectives, used by the resettable-replay reset. The
	 * resolution registry is never touched; only this clearable projection.
	 */
	void ClearPathFactAcrossPerspectives(FGameplayTag InputTag, FName PathIdentity);

	/**
	 * Idempotent registration of a node Instance under its ContextualTag key in LoadedNodeInstances. The map holds
	 * one entry per placement - a node's ContextualTag resolves to exactly one runtime instance (strict 1:1).
	 * Centralizing the Add keeps that invariant in one place; lookup and dedup-by-pointer sites rely on it.
	 *
	 * Behavior:
	 *   - Key unmapped: stores Instance under Key.
	 *   - Key already mapped to the SAME Instance: no-op - a benign idempotent re-register.
	 *   - Key already mapped to a DIFFERENT Instance: logs Warning and SKIPS the write. A collision here indicates
	 *     a broken invariant - ContextualTags are constructed at compile time to be unique per placement, so
	 *     surfacing the violation is preferable to a silent overwrite.
	 */
	void RegisterLoadedNodeInstance(FName Key, UQuestNodeBase* Instance);
		
	/**
	 * Pushes all per-perspective state-subsystem registrations for a freshly-registered node instance - its
	 * canonical ContextualTag plus every AssetScopedAliasTag. Per perspective: KnownQuests, alias mapping (when not
	 * the canonical), container classification, display data. Centralizes the registration bundle so the invariant
	 * "every perspective gets the full bookkeeping" lives in one place.
	 *
	 * Called by RegisterQuestlineGraph during instance registration. Idempotent across the underlying registrations:
	 * multiple placements of the same sub-questline share a class-channel AssetScopedAliasTag, so each placement
	 * registers it - re-registering an already-known tag / alias mapping / container classification / display-data
	 * record is a no-op or harmless overwrite.
	 */
	void RegisterAllNodePerspectives(const UQuestNodeBase* Instance) const;

	void PublishQuestEndedEvent(const UQuestNodeBase* Node, FGameplayTag OutcomeTag, EQuestResolutionSource Source, const FQuestEventPayload& ExternalContext = FQuestEventPayload(), const FQuestObjectiveActivationContext& CompleterContext = FQuestObjectiveActivationContext()) const;

	UPROPERTY()
	TObjectPtr<USignalSubsystem> QuestSignalSubsystem;
	UPROPERTY()
	TObjectPtr<UWorldStateSubsystem> WorldState;
	UPROPERTY()
	TObjectPtr<UQuestStateSubsystem> QuestStateSubsystem;

	/**
	 * Rich-record registry paired with WorldState's QuestState.<X>.Completed fact. Written atomically alongside
	 * WorldState in SetQuestResolved; read by catch-up paths on UQuestLifecycleObserver and UQuestObserverComponent.
	 * Holds the current session's resolution record per quest: outcome, timestamp, running count.
	 */
	UPROPERTY()
	TMap<FGameplayTag, FQuestResolutionRecord> QuestResolutions;
	
	/**
	 * Async-load a soft asset reference with weak-bound completion callback. Hot path (already-loaded
	 * asset; SoftPtr.Get() non-null) calls OnComplete synchronously on the same frame. Cold path
	 * schedules an async load via FStreamableManager and invokes OnComplete on load completion. Null
	 * SoftPtr fires OnComplete synchronously with nullptr so the contract is uniform regardless of input.
	 *
	 * WeakBindContext is the UObject the callback is weakly bound to - if it tears down mid-load,
	 * OnComplete becomes a no-op rather than a use-after-free - typically `this` when called from a
	 * UObject method.
	 *
	 * Post-load nullptr is tolerated (load failure, deleted-but-still-referenced asset); OnComplete
	 * receives the result either way and is responsible for any null check.
	 */
	template<CSoftLoadable TAsset>
	static void AsyncLoadAndActivate(UObject* WeakBindContext, const TSoftObjectPtr<TAsset>& SoftPtr, TFunction<void(TAsset*)> OnComplete)
	{
		if (SoftPtr.IsNull())
		{
			if (OnComplete) OnComplete(nullptr);
			return;
		}
		if (TAsset* HotResult = SoftPtr.Get())
		{
			if (OnComplete) OnComplete(HotResult);
			return;
		}
		FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
		const TSoftObjectPtr<TAsset> SoftCapture = SoftPtr;
		Streamable.RequestAsyncLoad(
			SoftPtr.ToSoftObjectPath(),
			FStreamableDelegate::CreateWeakLambda(WeakBindContext,
				[SoftCapture, OnCompleteCapture = MoveTemp(OnComplete)]()
				{
					if (OnCompleteCapture) OnCompleteCapture(SoftCapture.Get());
				}));
	}

	/**
	 * TSoftClassPtr counterpart of AsyncLoadAndActivate. Same hot/cold path split; OnComplete receives
	 * the resolved UClass* (nullptr-tolerant on load failure). Asset-typed return is replaced with
	 * UClass*; the caller can cast back to TSubclassOf<TClass> if needed.
	 */
	template<CSoftLoadable TClass>
	static void AsyncLoadAndActivateClass(UObject* WeakBindContext, const TSoftClassPtr<TClass>& SoftClassPtr, TFunction<void(UClass*)> OnComplete)
	{
		if (SoftClassPtr.IsNull())
		{
			if (OnComplete) OnComplete(nullptr);
			return;
		}
		if (UClass* HotResult = SoftClassPtr.Get())
		{
			if (OnComplete) OnComplete(HotResult);
			return;
		}
		FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
		const TSoftClassPtr<TClass> SoftCapture = SoftClassPtr;
		Streamable.RequestAsyncLoad(
			SoftClassPtr.ToSoftObjectPath(),
			FStreamableDelegate::CreateWeakLambda(WeakBindContext,
				[SoftCapture, OnCompleteCapture = MoveTemp(OnComplete)]()
				{
					if (OnCompleteCapture) OnCompleteCapture(SoftCapture.Get());
				}));
	}

	/**
	 * Assembles an outbound FQuestEventPayload from a node instance for publish sites that emit an
	 * FQuestEventBase-derived event. Sources:
	 *  - NodeInfo: copied from the node (compile-time identity + display metadata).
	 *  - CompletionTrigger: from the InCompletionTrigger argument. Default-constructed for non-completion
	 *     publishes (activation, progress, lifecycle); populated by callers emitting completion-flavored events.
	 *  - FQuestContextBase fields (Instigator / CustomData / OriginTag / OriginChain / OriginatingEventID):
	 *     forwarded from the Step's PendingActivationContext (pre-Live and Started phases) or Received-
	 *     ActivationContext (post-Started phases, after Pending is cleared at the tail of ActivateInternal).
	 *     See the buffer-selection comment in the cpp for details.
	 *
	 * No broadcast / no external hook - the function is a pure read from node state. CustomData / typed config
	 * extension flows in via the WRITE-INTO surfaces (BP-callable Params, request-event payloads); this helper
	 * reads it back out on the publisher side.
	 */
	FQuestEventPayload AssembleEventContext(const UQuestNodeBase* Node, const FQuestObjectiveTriggerContext& InCompletionTrigger) const;
	
	/**
	 * Wires the framework trigger→objective bridge for a live Step: subscribes the Step's tag to FQuestTriggerFiredEvent
	 * (routing to CheckQuestObjectives) and registers any target-class filters against the global trigger channel. Shared
	 * by the normal start path (HandleOnNodeStarted) and save restore (RestoreQuestlineGraph), so a restored objective
	 * receives trigger fires exactly as a freshly-started one does.
	 */
	void WireStepTriggerSubscriptions(UQuestStep* Step);
	
	UFUNCTION()
	void HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag, FName PathIdentity);
	UFUNCTION()
	void HandleOnNodeProgress(UQuestStep* Step, FQuestObjectiveTriggerContext ProgressData);
	UFUNCTION()
	void HandleOnNodeRefused(UQuestStep* Step, FGameplayTag RefusalReason, FQuestObjectiveTriggerContext TriggerContext);
	UFUNCTION()
	void HandleOnNodeTriggerDeactivation(UQuestStep* Step, FGameplayTag OutcomeTag, FQuestObjectiveTriggerContext FinalContext);
	UFUNCTION()
	void HandleOnNodeTriggerSatisfied(UQuestStep* Step, FQuestObjectiveTriggerContext Context);
	UFUNCTION()
	void HandleOnNodeStarted(UQuestNodeBase* Node, FGameplayTag InContextualTag);
	UFUNCTION()
	void HandleOnNodeActivationRefused(UQuestNodeBase* Node, FGameplayTag InContextualTag);
	
	/**
	 * Records a published progress refusal into the state subsystem's refusal history. Subscribed rather than called at
	 * each publish site because UQuestTriggerComponent publishes one of these too, and components do not write state -
	 * listening keeps the manager the only writer and covers any future publisher without another edit.
	 */
	void HandleProgressRefusedForRecord(FGameplayTag Channel, const FQuestProgressRefusedEvent& Event);

	/**
	 * Records a refused give into the same refusal history as a refused interaction. Separate handler rather than a shared
	 * template because the two payload types differ and a template over two call sites reads worse than the duplication.
	 */
	void HandleGiveBlockedForRecord(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event);
	
	UFUNCTION()
	void HandleOnNodeForwardActivated(UQuestNodeBase* Node);
	
	void HandleGiveQuestEvent(FGameplayTag Channel, const FQuestGivenEvent& Event);
	void HandleGiverRegisteredEvent(FGameplayTag Channel, const FQuestGiverRegisteredEvent& Event);
	void HandleNodeDeactivationRequest(FGameplayTag Channel, const FQuestDeactivateRequestEvent& Event);
	void HandleNodeDeactivatedEvent(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	void HandleActivationRequest(FGameplayTag Channel, const FQuestActivationRequestEvent& Event);
	void HandleBlockRequest(FGameplayTag Channel, const FQuestBlockRequestEvent& Event);
	void HandleClearBlockRequest(FGameplayTag Channel, const FQuestClearBlockRequestEvent& Event);
	void HandleResolveRequest(FGameplayTag Channel, const FQuestResolveRequestEvent& Event);
	void HandleQuestlineStartRequest(FGameplayTag Channel, const FQuestlineStartRequestEvent& Event);
	
	FDelegateHandle GivenDelegateHandle;
	FDelegateHandle GiverRegisteredDelegateHandle;
	FDelegateHandle DeactivateEventDelegateHandle;
	FDelegateHandle ActivationRequestDelegateHandle;
	FDelegateHandle BlockRequestDelegateHandle;
	FDelegateHandle ClearBlockRequestDelegateHandle;
	FDelegateHandle ResolveRequestDelegateHandle;
	FDelegateHandle QuestlineStartRequestDelegateHandle;
	FDelegateHandle ProgressRefusedRecordHandle;
	FDelegateHandle GiveBlockedRecordHandle;

	TMap<FGameplayTag, FDelegateHandle> LiveStepTriggerHandles;

	/** Per-node FQuestDeactivatedEvent subscription handles; populated in ActivateQuestlineGraph, cleaned up in Deinitialize. */
	TMap<FGameplayTag, FDelegateHandle> DeactivationSubscriptionHandles;

	void SetQuestLive(FGameplayTag QuestTag);

	/**
	 * Soft paths of questline graphs currently inside an ActivateQuestlineGraph cascade. Guards against
	 * cycles where a graph's activation reaches a Start Questline node targeting the same graph (direct
	 * self-reference) or a chain that returns to a graph already activating (A → B → A indirect). Set
	 * membership added on entry, removed on exit - non-cyclic nested activations (A's cascade activating
	 * a different graph B) push/pop cleanly. Re-entry while a graph's path is in the set means a cycle;
	 * the second activation logs and returns without re-running entry-tag firing.
	 */
	TSet<FSoftObjectPath> ActivatingGraphPaths;
	
	/**
	 * Recomputes a container's Live fact from its inner Step state. Called by SetQuestLive (and in upcoming
	 * phases by SetQuestResolved / SetQuestDeactivated) once a Step's Live state has changed - walks the Step's
	 * ancestor chain and re-derives each ancestor in turn. A container is Live whenever any inner Step (at any
	 * depth) has its Live fact set; not Live otherwise. Idempotent - checks current state and only mutates the
	 * WorldState fact when the derivation result differs.
	 */
	void DeriveContainerLive(FGameplayTag ContainerTag);

	/**
	 * Walks Step's ancestor wrappers and re-derives each one's Live fact. Covers both the Step's own
	 * compile-perspective ancestors (AncestorContainerTags) AND foreign-perspective ancestors derived
	 * from each AssetScopedAliasTag's parent prefix chain. The second walk is required because
	 * AuthoredGuid deduplication keeps only the canonical's compile data; outer-asset wrappers unique to a
	 * different compile (e.g. a LinkedQuestline wrapper in QL_Main that contextualizes QL_ActOne content
	 * where ActOne registered first) are absent from AncestorContainerTags and would never derive
	 * without alias-prefix fan-out. IsContainerTag bounds the walk to known wrappers - non-container
	 * prefix segments (asset roots, namespace prefixes) are skipped.
	 */
	void DeriveAllAncestorContainersForStep(UQuestStep* Step);
	
	void SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag, FName PathIdentity, EQuestResolutionSource Source, const FOriginatingEventID& OriginatingEventID = {});
	void SetQuestPendingGiver(FGameplayTag QuestTag);
	void ClearQuestPendingGiver(FGameplayTag QuestTag);

	/**
	 * Tears down an active or pending-giver node: cancels objectives, clears WorldState facts, writes Deactivated, then
	 * publishes FQuestDeactivatedEvent on the node tag channel so subscribers (givers, observers, and this subsystem's own
	 * HandleNodeDeactivatedEvent) can react. No-op on Completed nodes.
	 */
	void SetQuestDeactivated(FGameplayTag QuestTag, EDeactivationSource Source, const FQuestEventPayload& Context = FQuestEventPayload());

	/**
	 * Deactivated → Deactivate wiring, forwarded for every node a deactivation reaches - active or not. Split out of
	 * HandleNodeDeactivatedEvent so the pass-through path (which publishes no event) still relays the teardown.
	 */
	void CascadeDeactivation(FGameplayTag QuestTag, EDeactivationSource Source);

	/**
	 * Per-cascade guard for the deactivation cascade. Deactivation forwards through inactive nodes, so the old
	 * "not active" early-out no longer stops a loop-back or fan-in from re-forwarding forever. Visited entries clear
	 * when the root SetQuestDeactivated call unwinds (depth returns to 0).
	 */
	TSet<FGameplayTag> DeactivationCascadeVisited;
	int32 DeactivationCascadeDepth = 0;

	void RegisterGiversFromAssetRegistry();	
	
	/**
	 * Quest tags for which at least one QuestGiverComponent Blueprint exists in the project. Populated once at Initialize
	 * from the asset registry. A node whose tag is in this set waits for a give event rather than activating immediately.
	 */
	TSet<FGameplayTag> RegisteredGiverQuestTags;
	
	TMultiMap<FGameplayTag, UClass*> ClassFilteredSteps;
	FDelegateHandle ClassBridgeHandle;

	void CheckClassObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent);
	
	/**
	 * Inverted index built at Initialize from each UQuestlineGraph asset's ListenerGroupTags AR metadata:
	 * GroupTag → SoftObjectPaths of every graph whose listeners subscribe to that tag. Drives the reachability-walked
	 * async-load: when RegisterQuestlineGraph runs for a graph, the graph's OutwardSetterGroupTags get matched against
	 * this index, and any listed listener-graph not yet loaded is async-loaded + registered (which in turn cascades
	 * through its own setters).
	 */
	TMap<FGameplayTag, TArray<FSoftObjectPath>> GraphsByListenerGroupTag;

	/**
	 * Cycle / deduplication guard for the reachability cascade. Marked at WarmReachableGraphs entry (before async dispatch)
	 * so recursive RegisterQuestlineGraph → WarmReachableGraphs chains can detect already-in-flight loads and skip.
	 * Tracks SoftObjectPaths rather than UQuestlineGraph* pointers so a graph counts as loaded the moment its load is
	 * scheduled, not just when it finishes - prevents N parallel async-loads for the same target during a fan-in.
	 */
	TSet<FSoftObjectPath> KnownLoadedGraphPaths;

	/**
	 * Pending-restore stash for a level-transition load: set by USimpleQuestBlueprintLibrary::ApplyQuestSnapshot before
	 * OpenLevel, consumed after the target level is up - RestoreQuestGraphs drains the graph list, and each
	 * RestoreQuestlineGraph drains the deferred-activation entries for its own nodes. GameInstance-persistent, so it
	 * survives the transition between applying the data and rebuilding the graphs.
	 */
	TArray<FSoftObjectPath> PendingRestoreGraphs;
	TMap<FGuid, FQuestObjectiveRuntimeContext> PendingDeferredActivations;
	TMap<FGuid, FSimpleQuestObjectiveSaveState> PendingObjectiveStates;

	/**
	 * Scans the asset registry for UQuestlineGraph assets and builds GraphsByListenerGroupTag from each asset's
	 * ListenerGroupTags AR metadata. Called once from Initialize (synchronously if AR is ready; via OnFilesLoaded
	 * delegate otherwise). Loads no assets itself - the index just maps tag → SoftObjectPath, and async-loading
	 * happens lazily during reachability walks triggered by RegisterQuestlineGraph. Replaces the previous
	 * AutoLoadListenerBearingGraphs which sync-loaded every listener-bearing graph at startup.
	 */
	void BuildListenerGroupIndex();

	/**
	 * Walks a just-registered graph's OutwardSetterGroupTags, looks up matching listener-graphs in the global
	 * GraphsByListenerGroupTag index, and async-loads + registers any that aren't already loaded. Cascades
	 * recursively as each newly-loaded graph's own setters identify further reachable listener graphs.
	 * KnownLoadedGraphPaths prevents cycles and parallel-fan-in over-loads.
	 */
	void WarmReachableGraphs(UQuestlineGraph* Graph);
	

	/*------------------------------------------------------------------------------------------------------------------
	 * Deferred Completion
	 *----------------------------------------------------------------------------------------------------------------*/

	/**
	 * Pending completion data for a step whose chain is deferred until prereqs satisfy. Carries both the runtime
	 * OutcomeTag (event payload axis) and the PathIdentity (structural routing axis) so the resumed chain calls
	 * ChainToNextNodes with the same arguments the immediate path would have used.
	 */
	struct FQuestDeferredCompletion
	{
		FGameplayTag OutcomeTag;
		FName PathIdentity = NAME_None;
	};

	// Key: Step tag; Value: Pending completion data (OutcomeTag + PathIdentity)
	TMap<FGameplayTag, FQuestDeferredCompletion> DeferredCompletions;

	// Subscription handles for deferred completion prerequisite monitoring
	TMap<FGameplayTag, TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>> DeferredCompletionPrereqHandles;

	void DeferChainToNextNodes(UQuestStep* Step, FGameplayTag OutcomeTag, FName PathIdentity);
	void OnDeferredCompletionPrereqAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
	void OnDeferredCompletionPrereqResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event);
	void OnDeferredCompletionPrereqEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event);
	void TryFireDeferredCompletion(FGameplayTag StepTag);

	/** Shared body for all OnDeferredCompletionPrereq*** handlers: try every deferred completion. */
	void TryFireAllDeferredCompletions();

	
	/*------------------------------------------------------------------------------------------------------------------
	 * Enablement Watches: bidirectional state tracker per giver-gated quest in PendingGiver state.
	 *
	 * When a giver-gated quest's activation wire arrives and its prereq expression is non-Always, an entry is
	 * registered here that subscribes to each prereq leaf on both Added AND Removed events. On any leaf change,
	 * the watch re-evaluates the prereq and compares to its last-known state. Transitions fire FQuestEnabledEvent
	 * (unsatisfied to satisfied) or FQuestDisabledEvent (satisfied to unsatisfied). Designers binding to both events
	 * get bidirectional UI sync.
	 *
	 * Entries persist for the entire PendingGiver lifetime. Cleared on give success, abandon, or Deinitialize.
	 *----------------------------------------------------------------------------------------------------------------*/

	struct FEnablementWatch
	{
		FName NodeTagName;
		bool bLastKnownSatisfied = false;
	};

	TMap<FGameplayTag, FEnablementWatch> EnablementWatches;
	TMap<FGameplayTag, TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>> EnablementWatchHandles;

	/**
	 * Per-quest map of "the giver actor that initiated the most-recent successful give". Populated in
	 * HandleGiveQuestEvent right before ActivateNodeByTag, consumed in HandleOnNodeStarted to populate the
	 * GiverActor field on FQuestStartedEvent. Cleared on consumption so a subsequent non-giver activation
	 * doesn't inherit a stale entry.
	 */
	TMap<FGameplayTag, TWeakObjectPtr<AActor>> RecentGiverActors;

	/**
	 * Tag-cycle guard for ChainToNextNodes recursion. Tracks tags currently mid-processing so the
	 * recursion through FireBoundaryCompletion → ChainToNextNodes can detect re-entry on the same
	 * wrapper tag and abort cleanly. Catches degenerate authoring topologies (e.g. ActivationGroup
	 * wired entry→exit with no gating step between, then the exit loops the parent wrapper).
	 */
	TSet<FName> ChainRecursionTags;
	
	/**
	 * Shared helper: routes a wrapper boundary completion through ChainToNextNodes so the wrapper's
	 * full outcome-chain processing fires (SetQuestResolved + PublishQuestEndedEvent + the wrapper's
	 * own destination wires). Called from both the resolution path (ChainToNextNodes's
	 * FireBoundaryCompletion lambda) and the utility-forward path (HandleOnNodeForwardActivated).
	 * Keeping both call sites symmetric ensures wrapper outcome wires fire regardless of which
	 * inner mechanism reached the boundary - Step completion vs utility forward (Set Blocked,
	 * Clear Blocked, Activation Group). Falls back to direct SetQuestResolved + publish if the
	 * wrapper instance isn't loaded for some reason.
	 *
	 * OriginatingEventID is inherited from the cascade and threaded through the recursive
	 * ChainToNextNodes call.
	 */
	void FireWrapperBoundaryCompletion(const FQuestBoundaryCompletion& BC, const FOriginatingEventID& OriginatingEventID = FOriginatingEventID(), const FQuestObjectiveActivationContext& InheritedForward = FQuestObjectiveActivationContext());

	/**
	 * Per-questline-asset resolution registry write + bus publish. Each FQuestGraphResolution entry carries the
	 * Exit's authored OutcomeTag (what the questline resolves WITH), distinct from any cascading path outcome
	 * that led to the Exit. Writes QSS resolution record + Completed fact + publishes FQuestEndedEvent on the
	 * questline asset's tag channel so questline-tag subscribers (Hierarchical or ExactMatch) receive a direct
	 * questline-level lifecycle event.
	 */
	void PublishGraphResolutions(const TArray<FQuestGraphResolution>& Resolutions, EQuestResolutionSource Source, const FQuestObjectiveActivationContext& CompleterContext);
	
	void RegisterEnablementWatch(FGameplayTag QuestTag, FName NodeTagName, const FPrerequisiteExpression& Expr, bool bInitialSatisfied);
	void OnEnablementLeafFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
	void OnEnablementLeafFactRemoved(FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event);
	void OnEnablementLeafResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event);
	void OnEnablementLeafEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event);
	void ReevaluateEnablementWatch(FGameplayTag QuestTag);
	void ClearEnablementWatch(FGameplayTag QuestTag);

	/**
	 * Shared body for all OnEnablementLeaf*** handlers: re-evaluate every active enablement watch.
	 * Per-channel filtering isn't worth the inverse-lookup cost; expression re-eval is cheap.
	 */
	void ReevaluateAllEnablementWatches();
};
