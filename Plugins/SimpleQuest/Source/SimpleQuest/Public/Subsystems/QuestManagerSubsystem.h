// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/OriginatingEventID.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "Quests/Types/PrereqLeafSubscription.h"
#include "QuestManagerSubsystem.generated.h"


struct FQuestActivationRequestEvent;
struct FQuestBlockRequestEvent;
struct FQuestBoundaryCompletion;
struct FQuestClearBlockRequestEvent;
struct FQuestDeactivateRequestEvent;
struct FQuestDeactivatedEvent;
struct FQuestEntryRecordedEvent;
struct FQuestEventContext;
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
 * This class manages quests and quest givers, loading and unloading quests as needed. It handles the registration of actors
 * who have a quest giver component when they begin play. 
 */
UCLASS(Blueprintable, config = Game)
class SIMPLEQUEST_API UQuestManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	friend class USimpleQuestBlueprintLibrary;
	
protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	
	void CheckQuestObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent);

	int32 GetQuestCompletionCount(FGameplayTag QuestTag) const;

	/**
	 * Registers a graph's compiled node instances into LoadedNodeInstances WITHOUT firing entry nodes.
	 * Wires CachedGameInstance, OnRegisteredWithManager (so e.g. UActivationGroupListenerNode can subscribe
	 * to its group signal channel at instance lifetime), the per-node delegate binds, the per-tag deactivation
	 * subscription, and the container-classification push to UQuestStateSubsystem. Used by both
	 * ActivateQuestlineGraph (which calls this then fires entry tags) and the startup autoload path for
	 * listener-bearing graphs picked up via HasActivationGroupListener asset registry scan. Not idempotent on
	 * the same graph — re-binding delegates and double-subscribing deactivation handlers; callers must deduplicate.
	 */
	virtual void RegisterQuestlineGraph(UQuestlineGraph* Graph);
	
	/** Registers all compiled node instances from the graph into LoadedNodeInstances and activates its entry nodes. */
	virtual void ActivateQuestlineGraph(UQuestlineGraph* Graph);

	/**
	 * Looks up the instance for NodeTagName in LoadedNodeInstances and activates it. Stamps Provenance onto the
	 * destination's PendingActivationParams so it rides through ActivateInternal's merge into ReceivedActivationParams;
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
	 *                            Stamped onto PendingActivationParams.IncomingOutcomeTag and consumed by the wrapper's
	 *                            inner-entry routing. Invalid (default) for non-cascade activations.
	 * @param IncomingSourceTag   FName of the specific parent source whose outcome fired. UQuest entry routing filters
	 *                            source-qualified entries against this tag so only the matching spec's entry step
	 *                            fires — enables per-source routing on duplicate paths. NAME_None (default) for
	 *                            non-cascade activations.
	 * @param bBypassGiverGate    When true, routes past the giver gate to Live without entering PendingGiver. Set by
	 *                            HandleGiveQuestEvent's give-completion re-activation; the player has already accepted,
	 *                            so re-entering PendingGiver would be incorrect. The structural giver-gated set still
	 *                            contains the tag for the next loop iteration / external re-activation. Defaults to
	 *                            false for all non-give paths.
	 *
	 * @see UQuestStateSubsystem::RecordEntry
	 * @see UQuestManagerSubsystem::HandleOnNodeStarted
	 * @see FQuestObjectiveActivationParams::Provenance
	 */
	virtual void ActivateNodeByTag(
		FName NodeTagName,
		EQuestActivationProvenance Provenance,
		FGameplayTag IncomingOutcomeTag = FGameplayTag(),
		FName IncomingSourceTag = NAME_None,
		bool bBypassGiverGate = false);
	
	/** Node instances from all loaded questline graph assets, keyed by tag. Populated by ActivateQuestlineGraph. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UQuestNodeBase>> LoadedNodeInstances;
	
	/**
	 * Parallel index: AuthoredNodeGuid → the FName key used in LoadedNodeInstances. Enforces a "one instance per
	 * authored node" invariant at registration time. The same authored node can be compiled into multiple questline
	 * assets (standalone compile of an asset + inlined compile when the asset is a LinkedQuestline target inside
	 * another asset). Both compiles produce separate UQuestNodeBase instances under different FName keys (different
	 * ContextualTag perspectives). Without dedup, both instances register, both have their delegates bound, and
	 * every event fires twice — manifests as watcher duplication, giver missed-add on loop, and other multi-tag
	 * delivery anomalies that look like broadcast bugs but root at duplicate registration.
	 *
	 * Excluded from this dedup: utility node keys ("Util_<guid>" prefix — per-context instances are intentional;
	 * different cascade behavior per perspective) and prereq rule monitors (already deduped via shared FName key
	 * in LoadedNodeInstances; see the comment on the FName-key dedup loop in RegisterQuestlineGraph).
	 */
	TMap<FGuid, FName> LoadedInstancesByAuthoredNodeGuid;

	/**
	 * Resolves a perspective-form FGameplayTag to the canonical (Instance->GetContextualTag()) the runtime uses
	 * for state facts and registry entries. Layer 2 dedup populates LoadedNodeInstances under the canonical key
	 * AND every alias key (all pointing at the same instance), so any-perspective input here resolves the same
	 * instance, and the instance's ContextualTag is always the canonical regardless of which key was used to
	 * look it up.
	 *
	 * Request-side BP APIs (DeactivateQuest, BlockQuest, ClearBlockedQuest, ResolveQuest) receive user-authored
	 * tags that may be any perspective. State facts are written and queried at the canonical, so handlers MUST
	 * resolve before acting — otherwise IsActiveLifecycle / IsBlocked / IsTerminal queries miss, and AddFact
	 * writes leak to alias-perspective fact tags that no query will ever read.
	 *
	 * Pass-through behavior: returns InputTag unchanged if not in LoadedNodeInstances (legacy, external,
	 * pre-registration, or unregistered tag). Callers must still handle invalid-tag fallthroughs.
	 */
	FGameplayTag ResolveToCanonicalTag(FGameplayTag InputTag) const;

	/**
	 * Adds (or removes) a state-leaf fact at the canonical perspective AND every AssetScopedAliasTag the
	 * instance carries, so direct WorldState->HasFact queries from any perspective find the fact. Mirrors
	 * the multi-channel publish model the bus uses for events — facts and events both ride every
	 * perspective so consumers don't need to alias-walk at every read site.
	 *
	 * InputTag is canonicalized internally for safety; callers may pass any-perspective form. State-fact
	 * tags for every alias perspective are pre-registered by FSimpleQuest::RegisterCompiledQuestTags (the
	 * compile-time CompiledQuestTags list carries both contextual and alias FNames), so RequestGameplayTag
	 * resolves cleanly at each perspective.
	 *
	 * Add/Remove must stay symmetric — each Add bumps the ref-count by N (1 + alias count); the paired
	 * Remove decrements by N. The boolean idempotency guards in SetQuest* check canonical to prevent
	 * double-bumps under cascade convergence — they don't need per-perspective awareness because canonical
	 * is the source-of-truth for "is this state already set".
	 */
	void AddStateFactAcrossPerspectives(FGameplayTag InputTag, EQuestStateLeaf Leaf);
	void RemoveStateFactAcrossPerspectives(FGameplayTag InputTag, EQuestStateLeaf Leaf);

	/**
	 * Folds a deduped (lost-on-AuthoredGuid-collision) instance's perspective tags into the existing canonical
	 * instance's alias set. Without this, the second-registered instance is dropped entirely and any cascade /
	 * subscriber bound to its perspective form has no runtime to resolve. After the merge, the canonical instance
	 * carries every perspective the deduped instance would have had — events publish on all forms, alias-walks
	 * find the canonical from any form, state subsystem queries resolve through cross-asset perspectives.
	 *
	 * Also populates LoadedNodeInstances under each merged alias key (same pointer, additional keys) so every
	 * existing direct-lookup site (Find / FindRef / Contains) resolves transparently from any perspective. No
	 * per-call-site alias-walk needed — iteration sites don't exist (verified) so duplicate visits aren't a
	 * concern.
	 */
	void MergePerspectiveTagsInto(UQuestNodeBase* Existing, FName ExistingCanonicalName, UQuestNodeBase* Incoming);
	
	/** Chains to next nodes after a node completes, using tag-based routing from NextNodesByPath / NextNodesOnAnyOutcome. */
	virtual void ChainToNextNodes(
		UQuestNodeBase* CompletedNode,
		FGameplayTag OutcomeTag,
		FName PathIdentity,
		const FOriginatingEventID& OriginatingEventID = FOriginatingEventID());

	void PublishQuestEndedEvent(const UQuestNodeBase* Node, FGameplayTag OutcomeTag, EQuestResolutionSource Source) const;

	UPROPERTY()
	TObjectPtr<USignalSubsystem> QuestSignalSubsystem;
	UPROPERTY()
	TObjectPtr<UWorldStateSubsystem> WorldState;
	UPROPERTY()
	TObjectPtr<UQuestStateSubsystem> QuestStateSubsystem;

	/**
	 * Rich-record registry paired with WorldState's QuestState.<X>.Completed fact. Written atomically alongside
	 * WorldState in SetQuestResolved; read by catch-up paths on UQuestEventSubscription and UQuestWatcherComponent.
	 * Holds the current session's resolution record per quest: outcome, timestamp, running count.
	 */
	UPROPERTY()
	TMap<FGameplayTag, FQuestResolutionRecord> QuestResolutions;

private:	
	/**
	 * Assembles a fully populated FQuestEventContext from a node instance.
	 * Stage 1: copies NodeInfo from the node.
	 * Stage 2: copies InObjectiveData (non-empty only for completion events).
	 * Stage 3: broadcasts OnAssembleEventContext for game code to fill GameData.
	 */
	FQuestEventContext AssembleEventContext(const UQuestNodeBase* Node, const FQuestObjectiveContext& InCompletionContext) const;
	
	UFUNCTION()
	void HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag, FName PathIdentity);
	UFUNCTION()
	void HandleOnNodeProgress(UQuestStep* Step, FQuestObjectiveContext ProgressData);
	UFUNCTION()
	void HandleOnNodeStarted(UQuestNodeBase* Node, FGameplayTag InContextualTag);
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

	TMap<FGameplayTag, FDelegateHandle> LiveStepTriggerHandles;

	/** Per-node FQuestDeactivatedEvent subscription handles; populated in ActivateQuestlineGraph, cleaned up in Deinitialize. */
	TMap<FGameplayTag, FDelegateHandle> DeactivationSubscriptionHandles;

	void SetQuestLive(FGameplayTag QuestTag);

	/**
	 * Recomputes a container's Live fact from its inner Step state. Called by SetQuestLive (and in upcoming
	 * phases by SetQuestResolved / SetQuestDeactivated) once a Step's Live state has changed — walks the Step's
	 * ancestor chain and re-derives each ancestor in turn. A container is Live whenever any inner Step (at any
	 * depth) has its Live fact set; not Live otherwise. Idempotent — checks current state and only mutates the
	 * WorldState fact when the derivation result differs.
	 */
	void DeriveContainerLive(FGameplayTag ContainerTag);

	/**
	 * Walks Step's ancestor wrappers and re-derives each one's Live fact. Covers both the Step's own
	 * compile-perspective ancestors (AncestorContainerTags) AND foreign-perspective ancestors derived
	 * from each AssetScopedAliasTag's parent prefix chain. The second walk is required because
	 * AuthoredGuid dedup keeps only the canonical's compile data; outer-asset wrappers unique to a
	 * different compile (e.g. a LinkedQuestline wrapper in QL_Main that contextualizes QL_ActOne content
	 * where ActOne registered first) are absent from AncestorContainerTags and would never derive
	 * without alias-prefix fan-out. IsContainerTag bounds the walk to known wrappers — non-container
	 * prefix segments (asset roots, namespace prefixes) are skipped.
	 */
	void DeriveAllAncestorContainersForStep(UQuestStep* Step);
	
	void SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag, EQuestResolutionSource Source);
	void SetQuestPendingGiver(FGameplayTag QuestTag);
	void ClearQuestPendingGiver(FGameplayTag QuestTag);

	/**
	 * Tears down an active or pending-giver node: cancels objectives, clears WorldState facts, writes Deactivated, then
	 * publishes FQuestDeactivatedEvent on the node tag channel so subscribers (givers, watchers, and this subsystem's own
	 * HandleNodeDeactivatedEvent) can react. No-op on Completed nodes.
	 */
	void SetQuestDeactivated(FGameplayTag QuestTag, EDeactivationSource Source);

	void RegisterGiversFromAssetRegistry();
	
	/**
	 * Scans the asset registry for UQuestlineGraph assets flagged HasActivationGroupListener=true (stamped by the
	 * compiler whenever a graph compiles to ≥1 UActivationGroupListenerNode instance), sync-loads each, and calls
	 * RegisterQuestlineGraph on it. The asset's Start node is NOT fired — the goal is to bring the listener
	 * instances online (CachedGameInstance set, OnRegisteredWithManager called, signal subscriptions live) without
	 * activating the rest of the graph. Called once from Initialize (synchronously if AR is ready; via AR's
	 * OnFilesLoaded delegate otherwise) so every project-wide listener is armed before any caller can publish a
	 * setter event. Idempotent across re-entry: RegisterQuestlineGraph's per-node LoadedNodeInstances.Contains
	 * dedup makes a second call on the same graph a no-op.
	 */
	void AutoLoadListenerBearingGraphs();

	/**
	 * Quest tags for which at least one QuestGiverComponent Blueprint exists in the project. Populated once at Initialize
	 * from the asset registry. A node whose tag is in this set waits for a give event rather than activating immediately.
	 */
	TSet<FGameplayTag> RegisteredGiverQuestTags;
	
	TMultiMap<FGameplayTag, UClass*> ClassFilteredSteps;
	FDelegateHandle ClassBridgeHandle;

	void CheckClassObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent);


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
	 * inner mechanism reached the boundary — Step completion vs utility forward (Set Blocked,
	 * Clear Blocked, Activation Group). Falls back to direct SetQuestResolved + publish if the
	 * wrapper instance isn't loaded for some reason.
	 *
	 * OriginatingEventID is inherited from the cascade and threaded through the recursive
	 * ChainToNextNodes call.
	 */
	void FireWrapperBoundaryCompletion(const FQuestBoundaryCompletion& BC, const FOriginatingEventID& OriginatingEventID = FOriginatingEventID());

	/**
	 * Publishes a resolution event on each questline asset's identity tag in GraphTags via UQuestStateSubsystem::
	 * RecordResolution. Called from ChainToNextNodes before BoundaryCompletions fire, so the inner asset
	 * publishes its resolution before the wrapper-boundary cascade activates outer-asset destinations
	 * (cascade-direction event-order invariant: inner-first on outward flow). Symmetric with how an inner
	 * Quest container publishes FQuestEndedEvent on its own tag when its inner chain reaches an Exit —
	 * the asset-level equivalent uses the resolution registry's standard publish path.
	 */
	void PublishGraphResolutions(const TArray<FGameplayTag>& GraphTags, FGameplayTag OutcomeTag, EQuestResolutionSource Source);
	
	void RegisterEnablementWatch(FGameplayTag QuestTag, FName NodeTagName, const FPrerequisiteExpression& Expr, bool bInitialSatisfied);
	void OnEnablementLeafFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
	void OnEnablementLeafFactRemoved(FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event);
	void OnEnablementLeafResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event);
	void OnEnablementLeafEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event);
	void ReevaluateEnablementWatch(FGameplayTag QuestTag);
	void ClearEnablementWatch(FGameplayTag QuestTag);

	/** Shared body for all OnEnablementLeaf*** handlers: re-evaluate every active enablement watch.
	Per-channel filtering isn't worth the inverse-lookup cost; expression re-eval is cheap. */
	void ReevaluateAllEnablementWatches();
};
