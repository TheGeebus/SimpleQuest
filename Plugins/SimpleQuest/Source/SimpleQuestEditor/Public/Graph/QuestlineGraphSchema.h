// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "EdGraph/EdGraphSchema.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "QuestlineGraphSchema.generated.h"

class UQuestlineNode_Knot;
struct FGraphPanelPinConnectionFactory;
class UQuestlineNode_ContentBase;


UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	UQuestlineGraphSchema();
	
	/** Called when the graph is first created — populates it with the entry node */
	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;
	
	static TSharedPtr<FGraphPanelPinConnectionFactory> MakeQuestlineConnectionFactory();
		
	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
		const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const override;
	
	/** Populates the right-click context menu for the graph background */
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;

	/** Populates the right-click context menu when dragging off a pin */
	virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	/** Handle double-clicking a wire */
	virtual void OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2f& GraphPosition) const override;
	
	/** Returns the color to show on this pin type */
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

	/**
	 * Entry point for creating a connection between two pins. Internally calls CanCreateConnection, which performs validation of
	 * the intended connection against the rules for graphs using this schema and generates the connection response. Overridden to
	 * allow self-loop behavior where additional knots are placed to control Outcome wires looping back to the same node's Activate pin.
	 * 
	 * @param A The first pin.
	 * @param B The second pin.
	 * @return True if a connection was made/broken (graph was modified); false if the connection failed and had no side effects.
	 */
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;

	/**
	 * The engine of connection rules validation. Takes in two pins and returns a struct containing the ECanCreateConnectionResponse
	 * enum response value (MAKE/DISALLOW/BREAK_OTHERS etc.) and any appropriate error flagging and messages. Don't call this directly,
	 * prefer TryCreateConnection as the entry point for connection validation.
	 *
	 * Connection rules for a Questline Graph are based on graph topology: which destination a pin feeds, what other wires already
	 * reach that destination, and whether parallel paths would result - rather than by pin type as with a standard blueprint. This
	 * allows output pins to send Activation, Prerequisite, or Deactivation signals based on the downstream input connection type
	 * rather than hardcoding signal type per output pin. Works in tandem with graph compile-time validation to ensure all
	 * connections are sound and resolvable at runtime.
	 * 
	 * @param A The pin dragged from.
	 * @param B The pin dragged to.
	 * @return A response struct, containing the validation response along with any relevant error tooltip message and flag.
	 */
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;

	// Optional integration point for SimpleQuestEditorEN.
	static void RegisterENPolicyFactory(TFunction<FConnectionDrawingPolicy*(int32, int32, float, const FSlateRect&, FSlateWindowElementList&, UEdGraph*)> Factory);
	static void UnregisterENPolicyFactory();
	static bool IsENPolicyFactoryActive();

	// ---- Static helpers for setting and retrieving the pin the user dragged from ----
	
	static void SetActiveDragFromPin(UEdGraphPin* Pin);
	static UEdGraphPin* GetActiveDragFromPin();
	static void ClearActiveDragFromPin();

private:
	TUniquePtr<FQuestlineGraphTraversalPolicy> TraversalPolicy;

	/*------------------------------------------------------------------------------------------*
	 * Connection rule static helper functions
	 *------------------------------------------------------------------------------------------*/

	/** Backward (upstream) traversal: collect all non-knot output pins that ultimately feed into Pin through knot chains. */
	static void CollectUpstreamSources(const UEdGraphPin* Pin, TSet<const UEdGraphPin*>& OutSources, TSet<const UEdGraphPin*>& Visited);

	/** Forward (downstream) traversal: does OutputPin reach a pin of Category on TargetNode, tracing through knot chains? */
	static bool PinReachesCategory(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, FName Category, TSet<const UEdGraphPin*>& Visited);

	/** Combined: does any upstream source of OutputPin already reach Category on TargetNode? */
	static bool AnySourceReachesCategory(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, FName Category);

	/** Follows forward from StartNode through prerequisite pins and knots. Returns true if TargetNode is reachable in the prereq subgraph. */
	static bool PrereqChainReachesNode(const UEdGraphNode* StartNode, const UEdGraphNode* TargetNode, TSet<const UEdGraphNode*>& Visited);

	/** Walks backward through prereq inputs and knots, collecting non-prereq leaf source pins. */
	static void CollectPrereqLeafSources(const UEdGraphNode* Node, TSet<const UEdGraphPin*>& OutSources, TSet<const UEdGraphNode*>& Visited);

	/**
	 * Returns true if a source pin represents any outcome on its owning node: either a content-node Any Outcome pin or an Entry
	 * node's Any Outcome pin.
	 */
	static bool IsAnyOutcomeSource(const UEdGraphPin* Pin);
	
	/**
	 * Unified collision test between two signal source pins. Source identity is (owning_node, outcome). AnyOutcome on either
	 * side of a same-node comparison absorbs any specific outcome on the other. Different owning nodes never collide. Used
	 * by every connection-rule dedupe site so AnyOutcome and specific-outcome wires interact consistently.
	 */
	static bool PinsRepresentSameSignal(const UEdGraphPin* A, const UEdGraphPin* B);

	/** Returns true if any element of SetA collides (per PinsRepresentSameSignal) with any element of SetB. */
	static bool SignalSetsCollide(const TSet<const UEdGraphPin*>& SetA, const TSet<const UEdGraphPin*>& SetB, const UEdGraphPin*& OutCollisionA, const UEdGraphPin*& OutCollisionB);
	
	/**
	 * Check if this knot leads to a prerequisite-type pin downstream: an actual content node's Prerequisites pin, a prerequisite
	 * expression combinator (AND/OR/NOT), or a Prerequisite Group setter. Adds other knots that it encounters to the stack to be
	 * checked, so it walks forward through any number of downstream knots and the branches they may create. Knot-to-knot chains
	 * return true when an intermediate knot already has its PinType.PinCategory inferred as QuestPrerequisite, since that label
	 * implies it transitively reaches a prereq input. Only walks knot chains. Non-knot destinations are checked for prereq category
	 * but not recursed into.
	 * @param StartKnot the knot from which to begin checking the downstream connections.
	 * @return TRUE if any of the knot's downstream connections lead to any Prerequisite-type input pin.
	 */
	static bool KnotLeadsToPrereq(const UQuestlineNode_Knot* StartKnot);
	
	// ----- Connection validation helpers  ----------------------------------------

	/** Direct prerequisite pin rules (both pins are non-knot, at least one is prereq category). */
	FPinConnectionResponse ValidatePrerequisiteConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* OutputNode, const UEdGraphNode* InputNode) const;

	/** Deactivated/Deactivate pin rules (both pins are non-knot, at least one is deactivation category). */
	FPinConnectionResponse ValidateDeactivationConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* OutputNode, const UEdGraphNode* InputNode) const;

	/** Knot/reroute routing: category matching, conflict mirroring, reachability, duplicate paths. */
	FPinConnectionResponse ValidateKnotConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* OutputNode, const UEdGraphNode* InputNode, bool bOutputIsKnot, bool bInputIsKnot) const;

	/** Rejects connections that would deliver the same source quest signal twice to an input pin. */
	FPinConnectionResponse CheckDuplicateSources(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin,	bool bOutputIsKnot) const;

	/**
	 * When a wire is being proposed into an activation group setter's input, walks forward from every same-graph getter sharing
	 * that setter's GroupTag to find all destination Activate/Deactivate pins, and rejects the connection if any destination
	 * already receives a colliding signal through a different path.
	 *
	 * Symmetric completion of CheckDuplicateSources: backward dedupe handles "new wire to destination"; this handles "new wire to
	 * group setter whose signal later reaches destinations via getters."
	 */
	FPinConnectionResponse CheckGroupSetterForwardReach(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* InputNode) const;
	
	/** Rejects connections that would create a parallel path to a downstream terminal via a reroute. */
	FPinConnectionResponse CheckDownstreamParallelPaths(const UEdGraphPin* OutputPin, const UEdGraphPin* KnotInputPin) const;

};