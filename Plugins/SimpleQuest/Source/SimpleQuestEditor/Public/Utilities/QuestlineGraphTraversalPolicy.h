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
    
    /**
     * Collect the effective outcome source pins feeding a wire, following pass-through semantics across knots, utility
     * nodes (SetBlocked/ClearBlocked forward pin), and activation-group setter/getter chains.
     *
     * Starting from SourcePin (the OUTPUT side of a wire about to be connected, or an existing output pin), walks backward
     * through all pass-through chains and group-tag dereferences, terminating at outcome pins on content nodes.
     *
     * For activation-group getter nodes, locates all setter nodes in the same graph sharing the same GroupTag and recurses
     * into each setter's condition/Activate input wires. Cross-graph group chains are NOT followed here, use compile-time
     * diagnostics for those.
     *
     * @param SourcePin    A wire's source-side (output) pin.
     * @param OutSources   Accumulating set of terminal outcome pins.
     * @param VisitedNodes Cycle guard across nodes.
     */
    void CollectEffectiveSources(const UEdGraphPin* SourcePin, TSet<const UEdGraphPin*>& OutSources, TSet<const UEdGraphNode*>& VisitedNodes) const;
    
    void CollectDownstreamTerminalInputs(const UEdGraphPin* KnotOutPin, TArray<const UEdGraphPin*>& OutTerminalPins, TSet<const UEdGraphNode*>& Visited) const;

    /**
	 * Forward walker for activation-signal chains. Starting from an output pin carrying a QuestActivation signal, walks through
	 * knots, utility Forward outputs, and activation-group setter Forward outputs, collecting the terminal input pins on content
	 * or exit nodes that ultimately receive the signal.
	 *
	 * Used to find all destinations reached through an activation-group chain when validating that wiring a new source into a
	 * group setter would not create a parallel path to any of those destinations.
	 *
	 * @param FromOutput    An activation-signal output pin to walk forward from.
	 * @param OutTerminals  Accumulating set of Activate or Deactivate input pins on content/exit nodes.
	 * @param VisitedNodes  Cycle guard across nodes.
	 */
    void CollectActivationTerminals(const UEdGraphPin* FromOutput, TSet<const UEdGraphPin*>& OutTerminals, TSet<const UEdGraphNode*>& VisitedNodes) const;

    FPinReachability ComputeForwardReachability(const UEdGraphPin* OutputPin) const;
    FPinReachability ComputeFullReachability(const UEdGraphPin* OutputPin, const UEdGraphNode* OutputNode) const;
    
    // ---- Classification API (virtual: policy is extensible) ----

    /** Returns true if Node should be treated as an exit terminal. Default: UQuestlineNode_Exit (any OutcomeTag). */
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
