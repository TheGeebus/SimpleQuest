// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/PrerequisiteExpression.h"
#include "Types/PrereqLeafSubscription.h"
#include "Types/QuestNodeInfo.h"
#include "Types/QuestObjectiveRuntimeContext.h"
#include "Types/OriginatingEventID.h"
#include "Types/QuestGraphResolution.h"
#include "QuestNodeBase.generated.h"


struct FWorldStateFactAddedEvent;
struct FWorldStateFactRemovedEvent;
struct FQuestResolutionRecordedEvent;
struct FQuestEntryRecordedEvent;

class UQuestDisplayData;


/**
 * A single source-filtered entry destination. DestTag is the tag of a step or sub-node to activate when the parent quest
 * enters via a matching outcome from a matching source. SourceFilter is the compiled ContextualTag (as FName) of the specific
 * source node required to fire this entry; a mismatch skips the entry. No "any source" sentinel, every compiled entry
 * must carry a concrete SourceFilter.
 */
USTRUCT(BlueprintType)
struct FQuestEntryDestination
{
    GENERATED_BODY()

    UPROPERTY(VisibleDefaultsOnly)
    FName DestTag;

    UPROPERTY(VisibleDefaultsOnly)
    FName SourceFilter;
};

/**
 * Value type for Quest's EntryStepTagsByPath map. Wraps a per-path list of source-filtered destinations: one
 * entry per Entry-node output pin that fires for this path, each tagged with the parent source required to fire it.
 */
USTRUCT(BlueprintType)
struct FQuestEntryRouteList
{
    GENERATED_BODY()

    UPROPERTY(VisibleDefaultsOnly)
    TArray<FQuestEntryDestination> Destinations;
};

/**
 * One boundary crossing fired during ChainToNextNodes. When an inner-graph outcome routes through a
 * LinkedQuestline placement's Exit, the wrapper itself is treated as resolving with the matching outcome.
 * SetQuestResolved fires on WrapperTag (writes Completed and Path facts, records resolution history) and
 * FQuestEndedEvent publishes on the wrapper's tag. Compile-time populated by the LinkedQuestline branch and
 * accumulated by ResolvePinToTags as the walk crosses Exits; runtime consumed by ChainToNextNodes before
 * activating destination nodes (boundary facts must exist before downstream prereq evaluation).
 */
USTRUCT(BlueprintType)
struct FQuestBoundaryCompletion
{
    GENERATED_BODY()

    /**
     * The LinkedQuestline wrapper's compiled quest tag (boundary's outer-side identity). FName form for
     * compile-time storage; ChainToNextNodes resolves to FGameplayTag at runtime.
     */
    UPROPERTY(VisibleDefaultsOnly)
    FName WrapperTagName;

    /** The outcome the boundary completed with. Matches the crossed Exit's OutcomeTag. */
    UPROPERTY(VisibleDefaultsOnly)
    FGameplayTag OutcomeTag;

    bool operator==(const FQuestBoundaryCompletion& Other) const
    {
        return WrapperTagName == Other.WrapperTagName && OutcomeTag == Other.OutcomeTag;
    }
};

USTRUCT(BlueprintType)
struct FQuestPathNodeList
{
    GENERATED_BODY()

    UPROPERTY(VisibleDefaultsOnly)
    TArray<FName> NodeTags;

    /**
     * Boundary completions to fire when chaining through this path. Each entry triggers SetQuestResolved on
     * the wrapper tag, publishing Path facts, Completed fact, and FQuestEndedEvent. Order is innermost-first
     * (deepest LinkedQuestline crosses first, outer levels follow) so ChainToNextNodes fires nested
     * boundaries in the right semantic order. Empty for paths that don't cross any LinkedQuestline boundary.
     */
    UPROPERTY(VisibleDefaultsOnly)
    TArray<FQuestBoundaryCompletion> BoundaryCompletions;
    
    /**
     * Per-Exit attribution of questline-asset resolutions reached via this path. Populated by the compiler
     * when a pin-walk visits an Exit at an asset's root scope; each entry pairs the asset identity with the
     * Exit's authored OutcomeTag. Read by ChainToNextNodes to drive PublishGraphResolutions's per-entry
     * outcome value - the questline resolves with the Exit's OutcomeTag, not the cascading path's outcome.
     */
    UPROPERTY(VisibleDefaultsOnly)
    TArray<FQuestGraphResolution> ResolvedGraphs;
};

/**
 * Compile-time reachability snapshot per Activate pin on a UQuest container. Populated by
 * FQuestlineGraphCompiler::ComputeContainerReachability via a precise routing walk filtered by structural
 * containment (cf. UQuest::ReachableStepsByActivatePin doc). Read by the path-aware giver gate.
 */
USTRUCT(BlueprintType)
struct FQuestReachableSteps
{
    GENERATED_BODY()

    UPROPERTY(VisibleDefaultsOnly)
    TArray<FGameplayTag> StepTags;
};

/**
 * A completion path's advertised rewards - the reward-node keys reachable from one of this node's outcome routes,
 * plus a display label for that path. Compile-time populated (mirrors ReachableStepsByActivatePin). Keyed by
 * PathIdentity in the owning node's map: NAME_None is the any-outcome route; other keys match NextNodesByPath.
 */
USTRUCT()
struct FQuestReachableRewards
{
    GENERATED_BODY()

    /** Human-readable path name for a preview UI: the outcome tag's leaf, the dynamic PathName, or "On completion". */
    UPROPERTY(VisibleDefaultsOnly, Category = "Quest|Rewards")
    FText PathLabel;

    /** Keys of the reward nodes reachable down this path (resolve via the graph's CompiledNodes). */
    UPROPERTY(VisibleDefaultsOnly, Category = "Quest|Rewards")
    TArray<FName> RewardNodeKeys;
};

UCLASS(Abstract, Blueprintable)
class SIMPLEQUEST_API UQuestNodeBase : public UObject
{
    GENERATED_BODY()

    friend class FQuestlineGraphCompiler;
    friend class UQuestManagerSubsystem;

public:
    DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnNodeStarted, UQuestNodeBase*, Node, FGameplayTag, InContextualTag);
    DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnNodeCompleted, UQuestNodeBase*, Node, FGameplayTag, OutcomeTag, FName, PathIdentity);
    DECLARE_DYNAMIC_DELEGATE_OneParam (FOnNodeForwardActivated, UQuestNodeBase*, Node);
    
    /**
     * Fired every time an activation attempt reaches this node and does not advance it - on the first arrival when a
     * prerequisite is unmet, and again on each wake-and-re-check that still fails. The manager records the attempt; the
     * node does not write state itself.
     *
     * The attempt leaves no other trace, because the node's own state is unchanged - which is what being refused means.
     * Without this, a surface asking "did something just try to advance this and fail?" has nothing to read.
     */
    DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnNodeActivationRefused, UQuestNodeBase*, Node, FGameplayTag, InContextualTag);

    FOnNodeStarted OnNodeStarted;
    FOnNodeCompleted OnNodeCompleted;
    FOnNodeActivationRefused OnNodeActivationRefused;

    virtual UWorld* GetWorld() const override;
    
    /**
     * True for the concrete UQuestStep leaf class - the only node type that bears intrinsic lifecycle state
     * (PendingGiver / Live / Deactivated mutually exclusive). Read by lifecycle methods to gate behavior that
     * applies only to state-bearing leaves (e.g. publishing FQuestStartedEvent on the per-Step channel,
     * mutating the Live boolean fact directly).
     */
    virtual bool IsStepNode() const { return false; }

    /**
     * True for UQuest container wrappers (inline Quest placements + LinkedQuestline placements). Container Live
     * is DERIVED from inner Step state rather than tracked as an intrinsic boolean fact - see UQuest::InnerStepTags
     * for the data backing the derivation. Read by lifecycle methods to skip direct Live-fact writes on containers
     * and instead route through the auto-propagation walk over UQuestStep::AncestorContainerTags.
     */
    virtual bool IsContainerNode() const { return false; }

    /**
     * Called by UQuestManagerSubsystem::ActivateQuestlineGraph after the instance has been added to LoadedNodeInstances and
     * RegisterWithGameInstance has set CachedGameInstance. Default is a no-op. Override for nodes whose subscription / wiring
     * needs to last the full instance lifetime rather than the wrapper's Live state. Used by UActivationGroupListenerNode to
     * subscribe to its group's signal channel; ResetTransientState clears any stale handle on PIE re-entry; BeginDestroy
     * unsubscribes on shutdown.
     */
    virtual void OnRegisteredWithManager() {}
    
    /**
     * Fired by utility nodes (SetBlocked, ClearBlocked, GroupSignalSetter, GroupSignalGetter) instead of OnNodeStarted/OnNodeCompleted.
     * Manager chains NextNodesOnForward without writing any lifecycle facts.
     */
    FOnNodeForwardActivated OnNodeForwardActivated;

    /**
     * Entry point for node activation. Base implementation evaluates PrerequisiteExpression against WorldState; activates immediately
     * if satisfied, otherwise defers. UQuestStep overrides this to bypass prerequisite gating for non-giver steps (prerequisites gate
     * progression or completion instead - see EPrerequisiteGateMode).
     */
    virtual void Activate(FGameplayTag InContextualTag);
    
    /** Resolve a raw, editor-time FName tag created by the graph compiler into the registered runtime FGameplayTag */
    void ResolveContextualTag(FName TagName);

    /**
     * Resolve a list of raw, editor-time FName tags created by the graph compiler into registered runtime
     * FGameplayTags for AssetScopedAliasTags - the array of asset-scoped routing aliases for cross-asset
     * subscribers. One alias per enclosing LinkedQuestline asset above the leaf, ordered outermost-first
     * (excluding the top-level compile asset whose perspective IS ContextualTag). Empty list for content
     * compiled at the top level of its asset.
     */
    void ResolveAssetScopedAliasTags(const TArray<FName>& TagNames);
    
protected:
    /**
     * Called when prerequisites are confirmed satisfied. Sets ContextualTag and fires OnNodeStarted. Override in subclasses
     * for additional activation behavior; always call Super::ActivateInternal first.
     */
    virtual void ActivateInternal(FGameplayTag InContextualTag);
    
    /**
     * Called by the manager when this node is deactivated while in the Live state. Override in subclasses to destroy
     * any running objectives and cancel subscriptions specific to the running lifecycle. Default implementation cancels
     * deferred prereq subscriptions. Always call Super::DeactivateInternal.
     */
    virtual void DeactivateInternal(FGameplayTag InContextualTag);

    /**
     * Returns true to opt into symmetric prereq leaf subscription - DeferActivation wires the FactRemovedHandler
     * alongside the added/resolution/entry handlers so NOT(Fact) leaves wake on fact removal. Default false matches
     * the monotonic content-node behavior. Override on subclasses whose semantic requires waking when a previously 
     * satisfied condition flips false again (e.g., UPrereqGateNode under the Path-vs-Fact ontology - Facts are
     * reversible state; Paths are append-only history).
     */
    virtual bool UseSymmetricPrereqSubscription() const { return false; }
    
    /**
     * Called by utility nodes instead of the normal activation/completion lifecycle. Default implementation fires OnNodeForwardActivated
     * so the manager can chain NextNodesOnForward. Utility nodes call this after completing their utility action.
     */
    virtual void ForwardActivation();

    /**
     * Clears every member set during an earlier Activate / Deactivate cycle. Called by
     * UQuestManagerSubsystem::ActivateQuestlineGraph before each PIE session wires the node back into a live subsystem -
     * the compiled instances persist across PIE sessions (they live on the UQuestlineGraph asset), so any delegate
     * handles or scratch state from a prior session are stale and must be dropped. Override on subclasses that add
     * their own transient members; always call Super first.
     */
    virtual void ResetTransientState();
    
    /**
     * Stable per-placement save key. Composed at compile time as CombineGuids(outer-placement-chain, AuthoredNodeGuid),
     * so the same authored node placed in N linked contexts yields N distinct keys - each placement is its own saved
     * instance. Never hand-edited.
     *
     * Save-key contract (for save-state tooling and data pipelines): this GUID is a one-way composition - you CANNOT
     * recover "which node, which placement" from the value alone. Save-state maps keyed by it (DeferredActivations,
     * ObjectiveStates) are therefore resolved to their nodes by looking the GUID up against the compiled node set
     * (the running game, or a resolver that holds the compiled graph, provides that forward map) - not by inverting the
     * key. AuthoredNodeGuid is the placement-independent component: two instances of the same authored node share it,
     * so it is the handle for "this node regardless of where it's placed." Runtime save STATE is thus addressed through
     * the compiled graph, not as free-standing human-readable rows - by design: a readable companion identity on save
     * rows was considered and deferred until a concrete consumer needs it.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FGuid QuestContentGuid;
    
    /**
     * Authored node identity - equals the editor node's QuestGuid before any placement-chain combination. Multi-tag-stable:
     * inlined and standalone instances of the same authored node share this value, whereas QuestContentGuid combines the
     * outer placement chain and differs per compile context. Used as the AuthoredNodeGuid component of the cascade event
     * ID that gates redundant wrapper-completion records under multi-tag fanout (see FOriginatingEventID, F.3 Chunk B).
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FGuid AuthoredNodeGuid;

    /**
     * True for runtime instances that originate from a UQuestlineNode_LinkedQuestline editor placement (inline Quest
     * placements stay false). Editor-time signal for the Outliner's per-kind styling, surviving asset save/load so the
     * panel can classify on first display without waiting for a recompile.
     */
    UPROPERTY()
    bool bIsLinkedQuestlinePlacement = false;

    /**
     * For a LinkedQuestline placement wrapper (bIsLinkedQuestlinePlacement == true), the identity tag of the inner
     * questline asset this wrapper instantiates - SimpleQuest.Questline.<InnerQuestlineID>, with no node-label leaf.
     * Empty for inline placements and for a wrapper whose linked asset failed to resolve. This is the runtime bridge
     * from a placement's ContextualTag to the inner asset's own identity: the inner asset is never loaded at runtime
     * (its nodes are inlined here), so its identity-keyed data - e.g. questline-level rewards in
     * LiveQuestlineRewardsByIdentity - is otherwise unreachable from the placement. Matches the identity the compiler
     * harvests the inner questline's rewards under.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FGameplayTag LinkedInnerIdentityTag;

    /**
     * Parent-context routing tag for this node. Compiler-stamped from the parent compile-context's TagPrefix + node
     * label, so nodes inside a LinkedQuestline placement carry the parent asset's prefix. Used for all event bus
     * routing on the contextualized channel. Cross-asset subscriber compatibility is provided by AssetScopedAliasTags
     * (the per-enclosing-asset perspectives) rather than a single standalone tag.
     *
     * Format: SimpleQuest.Questline.<ParentPath>.<...>.<SanitizedNodeLabel>
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FGameplayTag ContextualTag;
    
    /**
     * Asset-scoped routing aliases for cross-asset subscribers. One entry per enclosing LinkedQuestline asset
     * above the leaf, ordered outermost-first - i.e., for a chain Project → links X → links Y → links Z →
     * contains this node, the array is [X-perspective, Y-perspective, Z-perspective]. ContextualTag carries the
     * Project (top-level) perspective; the array carries every other perspective.
     *
     * Use case: a observer placed in a level binds to "SimpleQuest.Questline.Y.LinkZ.S" - Y's natural perspective on
     * Step S - and receives events from EVERY placement of Y across the project, regardless of how deeply Y is
     * nested in the parent compile chain. The bus's hierarchical walk handles parent-prefix subscription within
     * each tag's chain; multi-publish covers cross-chain subscribers.
     *
     * Empty for content compiled at the top level of its asset (no LinkedQuestline ancestor) - the multi-publish
     * silently degenerates to single publish on ContextualTag alone.
     *
     * Format per entry: SimpleQuest.Questline.<EnclosingAssetQuestlineID>.<RemainingLinkChain>.<...>.<SanitizedNodeLabel>
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FGameplayTag> AssetScopedAliasTags;
    
    /**
     * Transient scratch slot for activation-time context stamped by the manager before Activate runs. Populated by
     * ChainToNextNodes (cascade pre-stamp), HandleGiveQuestEvent, HandleActivationRequest, and ActivateNodeByTag's
     * Quest-boundary forwarder. Consumed and cleared by the concrete subclass during its activation (UQuestStep packs
     * it as the runtime half handed to the objective; UQuest forwards to inner entries). Not serialized - save/load
     * restoration republishes the activation event rather than persisting this stash.
     */
    UPROPERTY(Transient)
    FQuestObjectiveRuntimeContext PendingActivationContext;

    /**
     * Routing table keyed by completion path identity. For static K2 placements PathIdentity equals the outcome
     * tag's full FName; for dynamic placements it's the sanitized PathName authored on the K2 node. Either way
     * the FName uniquely identifies one completion route through this node.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TMap<FName, FQuestPathNodeList> NextNodesByPath;

    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TSet<FName> NextNodesOnAnyOutcome;   // always activated regardless of outcome

    /**
     * Rewards advertised per completion path, for "do this task, get this reward" UI. Compile-time populated by the
     * reward-manifest pass for any node that completes (Steps and containers); empty for everything else. NAME_None is
     * the any-outcome bucket; other keys match NextNodesByPath. Merge NAME_None with a specific path at the query.
     */
    UPROPERTY(VisibleDefaultsOnly, Category = "Quest|Rewards")
    TMap<FName, FQuestReachableRewards> ReachableRewardsByPath;

    /**
     * Boundary completions for the Any-Outcome path. Same semantic as FQuestPathNodeList::BoundaryCompletions.
     * Fires when the completed node's outcome doesn't match a named path and routing falls through to
     * Any-Outcome, with one or more LinkedQuestline boundary crossings along the way.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FQuestBoundaryCompletion> BoundaryCompletionsOnAnyOutcome;

    /**
     * Any-Outcome parallel to FQuestPathNodeList::ResolvedGraphs. Same per-Exit attribution shape - pairs of
     * (asset identity, Exit OutcomeTag) - populated when this node resolves on the Any-Outcome path.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FQuestGraphResolution> ResolvedGraphsOnAnyOutcome;
    
    /** Nodes to activate normally when this node deactivates (Deactivated output to Activate input). */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TSet<FName> NextNodesOnDeactivation;

    /** Nodes to deactivate when this node deactivates (Deactivated output to any Deactivate inputs). */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TSet<FName> NextNodesToDeactivateOnDeactivation;

    /** Nodes to activate as a pass-through (utility node chaining; no lifecycle writes). */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TSet<FName> NextNodesOnForward;
    
    /**
     * Boundary completions to fire when this utility node's forward output crosses one or more wrapper Exits.
     * Each entry triggers SetQuestResolved on the wrapper tag (Completed + Path facts + resolution record) and
     * publishes FQuestEndedEvent. Order is innermost-first - the compiler's ResolvePinToTags walk accumulates
     * deepest crosses first as it traverses outward. Empty for utility nodes whose forward output doesn't
     * cross a wrapper boundary.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FQuestBoundaryCompletion> BoundaryCompletionsOnForward;

    /**
     * Per-Exit attribution of questline-asset resolutions reached when this utility node's Forward output
     * cascade terminates at an Exit/Outcome at an asset's root scope. Sibling to BoundaryCompletionsOnForward.
     * Read by HandleOnNodeForwardActivated to drive PublishGraphResolutions when the utility's Forward
     * resolves the enclosing questline (typical case: a Prereq Gate whose Forward wires to an Outcome
     * terminal). Empty for utility nodes whose forward output doesn't reach an asset-root Exit.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FQuestGraphResolution> ResolvedGraphsOnForward;
    
    /**
     * A struct that holds the composable prerequisites for this quest graph node: the relevant tags representing events and their
     * required completion statuses along with the boolean-style logic by which they are combined.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FPrerequisiteExpression PrerequisiteExpression;
    
    /**
     * Effective per-run resettability, resolved by the compiler from the authored EResettableReplay tri-state via
     * the alias-hierarchy inherit walk. When true, this node's structural resolution is mirrored to a clearable
     * WorldState fact (the per-run projection) alongside the permanent registry record, and gates wired from it read
     * the mirror so they re-gate on replay. False (default) = permanent, registry-only - current behavior.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    bool bResettableReplay = false;
    
    /**
     * Whether completing this node should also complete the parent graph. Replaces bCompletesQuestline; works at any
     * graph depth.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    bool bCompletesParentGraph = false;
    
    UPROPERTY()
    TWeakObjectPtr<UGameInstance> CachedGameInstance;
    
    void DeferActivation(FGameplayTag InContextualTag);

    /**
     * Compiled display metadata. DisplayName baked by the compiler from the unsanitized editor node title; ContextualTag resolved at
     * runtime alongside the standalone ContextualTag field. Read by the manager when assembling outbound FQuestEventPayload.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FQuestNodeInfo NodeInfo;
    
    /**
     * UI-friendly title for this node. Compiler-populated from the matching UQuestlineNode_ContentBase's DisplayName
     * UPROPERTY at compile time; empty FText when the designer didn't author one. Empty passes through to the QSS
     * query as-is - no fallback to NodeLabel or to a derived leaf-name reformat. Empty means the designer chose not
     * to pipeline display content; UI consumers branch on IsEmpty if they want to hide the entry entirely.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Display")
    FText DisplayName;

    /**
     * Flavor / context blurb for this node. Compiler-populated from the matching UQuestlineNode_ContentBase's Description.
     * Empty by default. Queried via UQuestStateSubsystem::GetDisplayDescription.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Display")
    FText Description;

    /**
     * Optional richer UI metadata. Compiler-populated from the matching UQuestlineNode_ContentBase's DisplayData reference.
     * Adopter UI casts to its expected UQuestDisplayData subclass at consumption time. Queried via
     * UQuestStateSubsystem::GetDisplayData.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Display")
    TObjectPtr<UQuestDisplayData> DisplayData;

private:
    /** Stores the contextual tag while waiting for prerequisites to clear */
    FGameplayTag DeferredContextualTag;

    /** Per-leaf-channel subscription handles; cleared when prerequisites are satisfied */
    TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles> PrereqSubscriptionHandles;

    /**
     * Cascade event ID associated with the most recent wake-up of this node - either a cascade arrival
     * (stamped via PendingActivationContext.IncomingParams.OriginatingEventID) or a prereq-subscription wake-up
     * (from the triggering FQuestResolutionRecordedEvent / FQuestEntryRecordedEvent payload).
     * Read by subclasses with per-event-ID deduplication logic (UPrereqGateNode); invalid when the wake-up was
     * not cascade-driven (raw Fact event with no OriginatingEventID plumbed).
     */
    FOriginatingEventID LastIncomingEventID;

    void OnPrereqFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
    void OnPrereqFactRemoved(FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event);
    void OnPrereqResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event);
    void OnPrereqEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event);
    void TryActivateDeferred();

    /**
     * Set by the manager when this node enters PendingGiver state. When true, prerequisites gate actual activation (giver semantics).
     * When false, prerequisites gate progression only - activation is immediate.
     */
    bool bWasGiverGated = false;

    /**
     * One-shot prerequisite-bypass directive for the next Activate. Set by ActivateNodeByTag from its
     * bBypassPrerequisites parameter (re-stamped on every call, so it never goes stale); consumed and cleared in
     * Activate, which then skips prereq evaluation and tears down any pending deferral. Cleared in ResetTransientState.
     */
    bool bBypassPrerequisitesOnce = false;

public:
    /**
     * True while this node is prereq-deferred - armed on its prerequisite leaves, waiting to activate. Reliable across
     * content AND utility nodes: a Prereq Gate defers with an invalid contextual tag, so this - not DeferredContextualTag -
     * is the signal save/load uses to detect a node that must be re-armed on load.
     */
    bool IsAwaitingPrerequisite() const { return PrereqSubscriptionHandles.Num() > 0; }
    
    /**
     * Current evaluation of this node's prerequisite expression, with per-leaf detail. A pure read: it evaluates against the
     * subsystems it is handed and caches nothing, so a caller sees present truth rather than the last value some other path
     * happened to push. A node with no wired prerequisites reports bIsAlways and bSatisfied true over an empty Leaves array.
     *
     * Reports what the EXPRESSION says, deliberately ignoring bBypassPrerequisitesOnce: a one-shot activation bypass does not
     * make an unsatisfied prerequisite satisfied, and a surface that showed otherwise would hide the thing being inspected.
     */
    FQuestPrereqStatus GetPrerequisiteStatus(const UWorldStateSubsystem* WorldState, const UQuestStateSubsystem* StateSubsystem) const
    {
        return PrerequisiteExpression.EvaluateWithLeafStatus(WorldState, StateSubsystem);
    }
    
    const TArray<FName>* GetNextNodesForPath(FName PathIdentity) const;

    void RegisterWithGameInstance(UGameInstance* InGameInstance) { CachedGameInstance = InGameInstance; }

    /**
     * For a LinkedQuestline placement, the identity tag of the questline asset it embeds - the bridge from a
     * placement's CONTEXTUAL tag to the asset IDENTITY its questline-level rewards are filed under. Invalid on any node
     * that is not a placement.
     *
     * Public because closing the reward-query topology leak depends on callers outside the manager being able to follow
     * it; the field stays protected so only the compiler writes it.
     */
    FGameplayTag GetLinkedInnerIdentityTag() const { return LinkedInnerIdentityTag; }
    
    FORCEINLINE FGuid GetQuestGuid() const { return QuestContentGuid; }
    FORCEINLINE FGuid GetAuthoredNodeGuid() const { return AuthoredNodeGuid; }
    FORCEINLINE bool IsLinkedQuestlinePlacement() const { return bIsLinkedQuestlinePlacement; }
    FORCEINLINE FGameplayTag GetContextualTag() const { return ContextualTag; }
    FORCEINLINE const TArray<FGameplayTag>& GetAssetScopedAliasTags() const { return AssetScopedAliasTags; }
    FORCEINLINE const TSet<FName>& GetNextNodesOnAnyOutcome() const { return NextNodesOnAnyOutcome; }
    FORCEINLINE const TSet<FName>& GetNextNodesOnDeactivation() const { return NextNodesOnDeactivation; }
    FORCEINLINE const TSet<FName>& GetNextNodesToDeactivateOnDeactivation() const { return NextNodesToDeactivateOnDeactivation; }
    FORCEINLINE const TSet<FName>& GetNextNodesOnForward() const { return NextNodesOnForward; }
    FORCEINLINE bool DoesCompleteParentGraph() const { return bCompletesParentGraph; }
    FORCEINLINE bool IsResettableReplay() const { return bResettableReplay; }
    FORCEINLINE bool IsGiverGated() const { return bWasGiverGated; }
    FORCEINLINE const FQuestNodeInfo& GetNodeInfo() const { return NodeInfo; }
    FORCEINLINE const TMap<FName, FQuestPathNodeList>& GetNextNodesByPath() const { return NextNodesByPath; }
    /** Compile-time reward manifest: what each completion path of this node advertises. See ReachableRewardsByPath. */
    FORCEINLINE const TMap<FName, FQuestReachableRewards>& GetReachableRewardsByPath() const { return ReachableRewardsByPath; }
    FORCEINLINE const TArray<FQuestBoundaryCompletion>& GetBoundaryCompletionsOnAnyOutcome() const { return BoundaryCompletionsOnAnyOutcome; }
    FORCEINLINE const TArray<FQuestGraphResolution>& GetResolvedGraphsOnAnyOutcome() const { return ResolvedGraphsOnAnyOutcome; }
    FORCEINLINE const TArray<FQuestBoundaryCompletion>& GetBoundaryCompletionsOnForward() const { return BoundaryCompletionsOnForward; }
    FORCEINLINE const TArray<FQuestGraphResolution>& GetResolvedGraphsOnForward() const { return ResolvedGraphsOnForward; }
    FORCEINLINE const FPrerequisiteExpression& GetPrerequisiteExpression() const { return PrerequisiteExpression; }
    FORCEINLINE const FText& GetDisplayName() const { return DisplayName; }
    FORCEINLINE const FText& GetDescription() const { return Description; }
    FORCEINLINE UQuestDisplayData* GetDisplayData() const { return DisplayData; }
    FORCEINLINE const FOriginatingEventID& GetLastIncomingEventID() const { return LastIncomingEventID; }
    
};
