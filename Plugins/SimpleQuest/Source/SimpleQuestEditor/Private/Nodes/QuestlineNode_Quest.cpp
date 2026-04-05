// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Quest.h"

#include "Graph/QuestlineGraphSchema.h"

FText UQuestlineNode_Quest::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!NodeLabel.IsEmpty()) return NodeLabel;
	return NSLOCTEXT("SimpleQuestEditor", "QuestNodeDefaultTitle", "Quest");
}

void UQuestlineNode_Quest::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();
	CreateInnerGraph();
}

void UQuestlineNode_Quest::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	CreateInnerGraph();  // fresh graph, not a copy of the original
}

void UQuestlineNode_Quest::CreateInnerGraph()
{
	InnerGraph = NewObject<UEdGraph>(this, UEdGraph::StaticClass(), NAME_None, RF_Transactional);
	InnerGraph->Schema = UQuestlineGraphSchema::StaticClass();
	const UQuestlineGraphSchema* Schema = GetDefault<UQuestlineGraphSchema>();
	Schema->CreateDefaultNodesForGraph(*InnerGraph);
}

