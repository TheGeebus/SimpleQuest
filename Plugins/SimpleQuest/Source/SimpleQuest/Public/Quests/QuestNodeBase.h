// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/PrereqLeafSubscription.h"
#include "Quests/Types/QuestNodeInfo.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "QuestNodeBase.generated.h"


struct FWorldStateFactAddedEvent;
struct FQuestResolutionRecordedEvent;
struct FQuestEntryRecordedEvent;

class UQuestReward;



/**
 * A single source-filtered entry destination. DestTag is the tag of a step or sub-node to activate when the parent quest
 * enters via a matching outcome from a matching source. SourceFilter is the compiled QuestTag (as FName) of the specific
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
    void ResolveQuestTag(FName TagName);
    
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
     * True for runtime instances that originate from a UQuestlineNode_LinkedQuestline editor placement (inline Quest
     * placements stay false). Editor-time signal for the Outliner's per-kind styling, surviving asset save/load so the
     * panel can classify on first display without waiting for a recompile.
     */
    UPROPERTY()
    bool bIsLinkedQuestlinePlacement = false;

    /**
     * Standalone routing tag for this node. Generated by the compiler from the questline asset name and node label. Used
     * for all event bus routing.
     * 
     * Format: SimpleQuest.Quest.<QuestlineName>.<SanitizedNodeLabel>
     */
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
    FGameplayTag QuestTag;

    /**
     * Call-site routing tag assigned by the subsystem at activation time. Equals QuestTag for root-level and standalone
     * nodes. Differs for nodes inside a shared linked graph where the parent provides a unique prefix.
     */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly)
    FGameplayTag ContextualTag;
    
    /**
     * Transient scratch slot for activation-time params stamped by the manager before Activate runs. Populated by
     * ChainToNextNodes (cascade pre-stamp), HandleGiveQuestEvent, HandleActivationRequest, and ActivateNodeByTag's
     * Quest-boundary forwarder. Consumed and cleared by the concrete subclass during its activation (UQuestStep merges
     * additively with authored defaults; UQuest forwards to inner entries). Not serialized — save/load restoration
     * republishes the activation event rather than persisting this stash.
     */
    UPROPERTY(Transient)
    FQuestObjectiveActivationParams PendingActivationParams;

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
     * Compiled display metadata. DisplayName baked by the compiler from the unsanitized editor node title; QuestTag resolved at
     * runtime alongside the standalone QuestTag field. Read by the manager when assembling outbound FQuestEventContext.
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
    FORCEINLINE bool IsLinkedQuestlinePlacement() const { return bIsLinkedQuestlinePlacement; }
    FORCEINLINE FGameplayTag GetQuestTag() const { return QuestTag; }
    FORCEINLINE FGameplayTag GetContextualTag() const { return ContextualTag; }
    FORCEINLINE void SetContextualTag(const FGameplayTag InTag) { ContextualTag = InTag; }
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
};
