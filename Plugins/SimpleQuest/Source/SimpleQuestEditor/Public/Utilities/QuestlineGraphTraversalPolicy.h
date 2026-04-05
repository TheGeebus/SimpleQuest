// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraphNode;
class UEdGraphPin;
class UQuestlineNodeBase;
class UQuestlineNode_ContentBase;

/**
 * Graph traversal utility class for questline graphs. Non-virtual traversal methods implement a fixed walk-forward algorithm;
 * virtual classification methods determine how individual node types are interpreted during traversal.
 *
 * Override the classification methods to support custom node types without rewriting traversal logic. Register a custom subclass
 * via ISimpleQuestEditorModule::RegisterTraversalFactory — the compiler and schema both acquire their traversal instance from
 * the same factory, keeping classification consistent across the editor.
 *
 * @see FQuestlineGraphCompiler
 */
class SIMPLEQUESTEDITOR_API FQuestlineGraphTraversalPolicy
{
public:
    virtual ~FQuestlineGraphTraversalPolicy() = default;

    struct FPinReachability
    {
        bool bReachesExit    = false;
        bool bReachesContent = false;
        bool IsMixed() const { return bReachesExit && bReachesContent; }
    };

    // ---- Traversal API (non-virtual: algorithm is fixed) ----

    bool HasDownstreamExit(const UEdGraphPin* OutputPin, TSet<const UEdGraphNode*>& Visited) const;
    bool HasDownstreamContent(const UEdGraphPin* OutputPin, TSet<const UEdGraphNode*>& Visited) const;
    bool LeadsToNode(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, TSet<const UEdGraphNode*>& Visited) const;
    void CollectKnotInputSources(const UEdGraphPin* KnotInPin, TArray<const UEdGraphPin*>& OutSourcePins, TSet<const UEdGraphNode*>& Visited) const;
    void CollectSourceContentNodes(const UEdGraphPin* Pin, TSet<UQuestlineNode_ContentBase*>& OutSources, TSet<const UEdGraphNode*>& Visited) const;
    void CollectDownstreamTerminalInputs(const UEdGraphPin* KnotOutPin, TArray<const UEdGraphPin*>& OutTerminalPins, TSet<const UEdGraphNode*>& Visited) const;
    FPinReachability ComputeForwardReachability(const UEdGraphPin* OutputPin) const;
    FPinReachability ComputeFullReachability(const UEdGraphPin* OutputPin, const UEdGraphNode* OutputNode) const;
    
    // ---- Classification API (virtual: policy is extensible) ----

    /** Returns true if Node should be treated as a success exit terminal. Default: UQuestlineNode_Exit_Success. */
    virtual bool IsExitSuccessNode(const UEdGraphNode* Node) const;

    /** Returns true if Node should be treated as a failure exit terminal. Default: UQuestlineNode_Exit_Failure. */
    virtual bool IsExitFailureNode(const UEdGraphNode* Node) const;

    /** Returns true if Node should be treated as any kind of exit terminal. Default: IsExitSuccessNode || IsExitFailureNode. */
    virtual bool IsExitNode(const UEdGraphNode* Node) const;

    /**
     * Returns true if Node should be treated as a content terminal (Quest, Leaf, LinkedQuestline).
     * Override to include custom content node types.
     */
    virtual bool IsContentNode(const UEdGraphNode* Node) const;

    /**
     * Returns true if Node should be traversed through transparently (i.e., is a reroute/knot).
     * Override to include custom pass-through node types.
     */
    virtual bool IsPassThroughNode(const UEdGraphNode* Node) const;

    /**
     * Returns the outbound pin for a pass-through node. Called only when IsPassThroughNode returns true.
     * Default: finds the pin named "KnotOut" with EGPD_Output direction.
     */
    virtual const UEdGraphPin* GetPassThroughOutputPin(const UEdGraphNode* Node) const;

    /**
     * Returns the inbound pin for a pass-through node. Called only when IsPassThroughNode returns true.
     * Default: finds the pin named "KnotIn".
     */
    virtual const UEdGraphPin* GetPassThroughInputPin(const UEdGraphNode* Node) const;
};
