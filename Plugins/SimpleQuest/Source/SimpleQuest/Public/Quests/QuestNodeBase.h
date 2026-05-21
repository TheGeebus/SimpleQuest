// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/PrereqLeafSubscription.h"
#include "Quests/Types/QuestNodeInfo.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "QuestNodeBase.generated.h"


struct FWorldStateFactAddedEvent;
struct FQuestResolutionRecordedEvent;
struct FQuestEntryRecordedEvent;

class UQuestReward;



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

    /** The LinkedQuestline wrapper's compiled quest tag (boundary's outer-side identity). FName form for
     *  compile-time storage; ChainToNextNodes resolves to FGameplayTag at runtime. */
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
     * Questline asset identity tags whose root-scope Exit this path reaches. Populated by the compiler when
     * a pin-walk visits an Exit at an asset's root scope. Distinct from BoundaryCompletions: the BC list tells
     * ChainToNextNodes which wrapper(s) to cascade through; ExitedGraphTags tells it which questline assets
     * reached their terminus and should publish a resolution event on their identity tag. Inner-first on
     * outward flow — published before BoundaryCompletions fire. At the outermost root scope only this list
     * may be populated (BC list empty) — asset resolves with no wrapper to cascade to.
     */
    UPROPERTY(VisibleDefaultsOnly)
    TArray<FGameplayTag> ExitedGraphTags;
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

    FOnNodeStarted OnNodeStarted;
    FOnNodeCompleted OnNodeCompleted;

    virtual UWorld* GetWorld() const override;
    
    /**
     * True for the concrete UQuestStep leaf class — the only node type that bears intrinsic lifecycle state
     * (PendingGiver / Live / Deactivated mutually exclusive). Read by lifecycle methods to gate behavior that
     * applies only to state-bearing leaves (e.g. publishing FQuestStartedEvent on the per-Step channel,
     * mutating the Live boolean fact directly).
     */
    virtual bool IsStepNode() const { return false; }

    /**
     * True for UQuest container wrappers (inline Quest placements + LinkedQuestline placements). Container Live
     * is DERIVED from inner Step state rather than tracked as an intrinsic boolean fact — see UQuest::InnerStepTags
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
     * progression or completion instead — see EPrerequisiteGateMode).
     */
    virtual void Activate(FGameplayTag InContextualTag);
    
    /** Resolve a raw, editor-time FName tag created by the graph compiler into the registered runtime FGameplayTag */
    void ResolveContextualTag(FName TagName);

    /**
     * Resolve a list of raw, editor-time FName tags created by the graph compiler into registered runtime
     * FGameplayTags for AssetScopedAliasTags — the array of asset-scoped routing aliases for cross-asset
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
     * Called by utility nodes instead of the normal activation/completion lifecycle. Default implementation fires OnNodeForwardActivated
     * so the manager can chain NextNodesOnForward. Utility nodes call this after completing their utility action.
     */
    virtual void ForwardActivation();

    /**
     * Clears every member set during an earlier Activate / Deactivate cycle. Called by
     * UQuestManagerSubsystem::ActivateQuestlineGraph before each PIE session wires the node back into a live subsystem —
     * the compiled instances persist across PIE sessions (they live on the UQuestlineGraph asset), so any delegate
     * handles or scratch state from a prior session are stale and must be dropped. Override on subclasses that add
     * their own transient members; always call Super first.
     */
    virtual void ResetTransientState();
    
    /**
     * Stable save key. Derived from the authoring node's GUID at compile time. Never hand-edited. Forms part of the GUID
     * chain for save data keying in linked graphs.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FGuid QuestContentGuid;
    
    /**
     * Authored node identity — equals the editor node's QuestGuid before any placement-chain combination. Multi-tag-stable:
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
     * Parent-context routing tag for this node. Compiler-stamped from the parent compile-context's TagPrefix + node
     * label, so nodes inside a LinkedQuestline placement carry the parent asset's prefix. Used for all event bus
     * routing on the contextualized channel; will pair with StandaloneTag (Phase A of §1.4 dual-tag finalization)
     * for cross-asset subscriber compatibility.
     *
     * Format: SimpleQuest.Questline.<ParentPath>.<...>.<SanitizedNodeLabel>
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FGameplayTag ContextualTag;
    
    /**
     * Asset-scoped routing aliases for cross-asset subscribers. One entry per enclosing LinkedQuestline asset
     * above the leaf, ordered outermost-first — i.e., for a chain Project → links X → links Y → links Z →
     * contains this node, the array is [X-perspective, Y-perspective, Z-perspective]. ContextualTag carries the
     * Project (top-level) perspective; the array carries every other perspective.
     *
     * Use case: a observer placed in a level binds to "SimpleQuest.Questline.Y.LinkZ.S" — Y's natural perspective on
     * Step S — and receives events from EVERY placement of Y across the project, regardless of how deeply Y is
     * nested in the parent compile chain. The bus's hierarchical walk handles parent-prefix subscription within
     * each tag's chain; multi-publish covers cross-chain subscribers.
     *
     * Empty for content compiled at the top level of its asset (no LinkedQuestline ancestor) — the multi-publish
     * silently degenerates to single publish on ContextualTag alone.
     *
     * Format per entry: SimpleQuest.Questline.<EnclosingAssetQuestlineID>.<RemainingLinkChain>.<...>.<SanitizedNodeLabel>
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FGameplayTag> AssetScopedAliasTags;
    
    /**
     * Transient scratch slot for activation-time params stamped by the manager before Activate runs. Populated by
     * ChainToNextNodes (cascade pre-stamp), HandleGiveQuestEvent, HandleActivationRequest, and ActivateNodeByTag's
     * Quest-boundary forwarder. Consumed and cleared by the concrete subclass during its activation (UQuestStep merges
     * additively with authored defaults; UQuest forwards to inner entries). Not serialized — save/load restoration
     * republishes the activation event rather than persisting this stash.
     */
    UPROPERTY(Transient)
    FQuestObjectiveActivationContext PendingActivationContext;

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
     * Boundary completions for the Any-Outcome path. Same semantic as FQuestPathNodeList::BoundaryCompletions.
     * Fires when the completed node's outcome doesn't match a named path and routing falls through to
     * Any-Outcome, with one or more LinkedQuestline boundary crossings along the way.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FQuestBoundaryCompletion> BoundaryCompletionsOnAnyOutcome;

    /**
     * Any-Outcome parallel to FQuestPathNodeList::ExitedGraphTags. Same semantic — questline assets whose
     * root-scope Exit is reached when this node resolves on the Any-Outcome path.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FGameplayTag> ExitedGraphTagsOnAnyOutcome;

    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TSet<FName> NextNodesOnAbandon;       // DEPRECATED — remove after compiler migration
    
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
     * publishes FQuestEndedEvent. Order is innermost-first — the compiler's ResolvePinToTags walk accumulates
     * deepest crosses first as it traverses outward. Empty for utility nodes whose forward output doesn't
     * cross a wrapper boundary.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    TArray<FQuestBoundaryCompletion> BoundaryCompletionsOnForward;
    
    /**
     * A struct that holds the composable prerequisites for this quest graph node: the relevant tags representing events and their
     * required completion statuses along with the boolean-style logic by which they are combined.
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FPrerequisiteExpression PrerequisiteExpression;

    /** Reward granted on completion of this node. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TSoftClassPtr<UQuestReward> Reward;

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

private:
    // Stores the contextual tag while waiting for prerequisites to clear
    FGameplayTag DeferredContextualTag;

    // Per-leaf-channel subscription handles; cleared when prerequisites are satisfied
    TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles> PrereqSubscriptionHandles;

    void OnPrereqFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
    void OnPrereqResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event);
    void OnPrereqEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event);
    void TryActivateDeferred();

    /**
     * Set by the manager when this node enters PendingGiver state. When true, prerequisites gate actual activation (giver semantics).
     * When false, prerequisites gate progression only — activation is immediate.
     */
    bool bWasGiverGated = false;
    
public:
    FORCEINLINE FGuid GetQuestGuid() const { return QuestContentGuid; }
    FORCEINLINE FGuid GetAuthoredNodeGuid() const { return AuthoredNodeGuid; }
    FORCEINLINE bool IsLinkedQuestlinePlacement() const { return bIsLinkedQuestlinePlacement; }
    FORCEINLINE FGameplayTag GetContextualTag() const { return ContextualTag; }
    FORCEINLINE const TArray<FGameplayTag>& GetAssetScopedAliasTags() const { return AssetScopedAliasTags; }
    const TArray<FName>* GetNextNodesForPath(FName PathIdentity) const;
    FORCEINLINE const TSet<FName>& GetNextNodesOnAnyOutcome() const { return NextNodesOnAnyOutcome; }
    FORCEINLINE const TSet<FName>& GetNextNodesOnAbandon() const { return NextNodesOnAbandon; }
    FORCEINLINE const TSet<FName>& GetNextNodesOnDeactivation() const { return NextNodesOnDeactivation; }
    FORCEINLINE const TSet<FName>& GetNextNodesToDeactivateOnDeactivation() const { return NextNodesToDeactivateOnDeactivation; }
    FORCEINLINE const TSet<FName>& GetNextNodesOnForward() const { return NextNodesOnForward; }
    FORCEINLINE const TArray<FQuestBoundaryCompletion>& GetBoundaryCompletionsOnForward() const { return BoundaryCompletionsOnForward; }
    FORCEINLINE bool DoesCompleteParentGraph() const { return bCompletesParentGraph; }
    FORCEINLINE bool IsGiverGated() const { return bWasGiverGated; }
    void RegisterWithGameInstance(UGameInstance* InGameInstance) { CachedGameInstance = InGameInstance; }
    FORCEINLINE const FQuestNodeInfo& GetNodeInfo() const { return NodeInfo; }
    FORCEINLINE const TMap<FName, FQuestPathNodeList>& GetNextNodesByPath() const { return NextNodesByPath; }
    FORCEINLINE const TArray<FQuestBoundaryCompletion>& GetBoundaryCompletionsOnAnyOutcome() const { return BoundaryCompletionsOnAnyOutcome; }
    FORCEINLINE const TArray<FGameplayTag>& GetExitedGraphTagsOnAnyOutcome() const { return ExitedGraphTagsOnAnyOutcome; }
};
