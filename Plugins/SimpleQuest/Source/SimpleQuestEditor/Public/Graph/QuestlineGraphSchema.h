// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Exit_Success.h"
#include "Nodes/QuestlineNode_Exit_Failure.h"
#include "QuestlineGraphSchema.generated.h"

class UQuestlineNode_Quest;

UCLASS()
class UQuestlineGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	// Called when the graph is first created — populates it with the entry node
	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;

	// Find any source quest nodes of a given wire
	static void CollectSourceQuests(const UEdGraphPin* Pin, TSet<UQuestlineNode_Quest*>& OutSources, TSet<const UEdGraphNode*>& Visited);
	
	// Determines whether a connection between two pins is valid
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;

	// Populates the right-click context menu for the graph background
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;

	// Populates the right-click context menu when dragging off a pin
	virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// Returns the name to display for this graph type in the editor
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

	//virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	
	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(
	int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
	const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraphObj) const override;

};
