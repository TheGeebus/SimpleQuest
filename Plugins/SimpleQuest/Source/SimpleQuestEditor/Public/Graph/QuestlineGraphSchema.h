// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "QuestlineGraphSchema.generated.h"

UCLASS()
class UQuestlineGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	// Called when the graph is first created — populates it with the entry node
	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;

	// Determines whether a connection between two pins is valid
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;

	// Populates the right-click context menu for the graph background
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;

	// Populates the right-click context menu when dragging off a pin
	virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// Returns the name to display for this graph type in the editor
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
};
