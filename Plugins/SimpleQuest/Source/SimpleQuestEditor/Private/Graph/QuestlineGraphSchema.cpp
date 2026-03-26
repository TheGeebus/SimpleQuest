// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Graph/QuestlineGraphSchema.h"

void UQuestlineGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	// Entry node creation goes here in a later step
	// For now, start with an empty graph
}

const FPinConnectionResponse UQuestlineGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	if (!A || !B)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PinNull", "Invalid pin"));
	}

	// Disallow connections between pins on the same node
	if (A->GetOwningNode() == B->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "SameNode", "Cannot connect a node to itself"));
	}

	// Disallow connections between two pins of the same direction
	if (A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "SameDirection", "Cannot connect two inputs or two outputs"));
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}

void UQuestlineGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// Node creation actions go here
}

void UQuestlineGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	// Node-specific context menu actions go here
}

FLinearColor UQuestlineGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FLinearColor::White;
}
