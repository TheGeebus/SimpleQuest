// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "EdGraph/EdGraphSchema.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "QuestlineGraphSchema.generated.h"

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
		
	/** Determines whether a connection between two pins is valid */
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;

	/** Populates the right-click context menu for the graph background */
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;

	/** Populates the right-click context menu when dragging off a pin */
	virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	/** Handle double-clicking a wire */
	virtual void OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2f& GraphPosition) const override;
	
	/** Returns the color to show on this pin type */
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	
	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(
	int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
	const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraphObj) const override;
	
	static TSharedPtr<FGraphPanelPinConnectionFactory> MakeQuestlineConnectionFactory();

	// Optional integration point for SimpleQuestEditorEN.
	static void RegisterENPolicyFactory(TFunction<FConnectionDrawingPolicy*(int32, int32, float, const FSlateRect&, FSlateWindowElementList&, UEdGraph*)> Factory);
	static void UnregisterENPolicyFactory();
	static bool IsENPolicyFactoryActive();
		
	static void SetActiveDragFromPin(UEdGraphPin* Pin);
	static UEdGraphPin* GetActiveDragFromPin();
	static void ClearActiveDragFromPin();

private:
	static void CollectUpstreamSources(const UEdGraphPin* Pin, TSet<const UEdGraphPin*>& OutSources, TSet<const UEdGraphPin*>& Visited);
	static bool PinReachesCategory(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, FName Category, TSet<const UEdGraphPin*>& Visited);
	static bool AnySourceReachesCategory(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, FName Category);
	TUniquePtr<FQuestlineGraphTraversalPolicy> TraversalPolicy;

	// ── Connection validation helpers (extracted from CanCreateConnection) ──

	/** Direct prerequisite pin rules (both pins are non-knot, at least one is prereq category). */
	FPinConnectionResponse ValidatePrerequisiteConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* OutputNode, const UEdGraphNode* InputNode) const;

	/** Deactivated/Deactivate pin rules (both pins are non-knot, at least one is deactivation category). */
	FPinConnectionResponse ValidateDeactivationConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* OutputNode, const UEdGraphNode* InputNode) const;

	/** Knot/reroute routing: category matching, conflict mirroring, reachability, duplicate paths. */
	FPinConnectionResponse ValidateKnotConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* OutputNode, const UEdGraphNode* InputNode, bool bOutputIsKnot, bool bInputIsKnot) const;

	/** Rejects connections that would deliver the same source quest signal twice to an input pin. */
	FPinConnectionResponse CheckDuplicateSources(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin,	bool bOutputIsKnot) const;

	/** Rejects connections that would create a parallel path to a downstream terminal via a reroute. */
	FPinConnectionResponse CheckDownstreamParallelPaths(const UEdGraphPin* OutputPin, const UEdGraphPin* KnotInputPin) const;

};