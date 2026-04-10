// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Quest.h"

#include "Graph/QuestlineGraphSchema.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Utilities/SimpleQuestEditorUtils.h"


void UQuestlineNode_Quest::AllocateDefaultPins()
{
	RebuildOutcomePinsFromInnerGraph();
	Super::AllocateDefaultPins();
}

FText UQuestlineNode_Quest::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!NodeLabel.IsEmpty()) return NodeLabel;
	return NSLOCTEXT("SimpleQuestEditor", "QuestNodeDefaultTitle", "Quest");
}

FLinearColor UQuestlineNode_Quest::GetNodeTitleColor() const
{
	return SQ_ED_NODE_QUEST;
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

void UQuestlineNode_Quest::RebuildOutcomePinsFromInnerGraph()
{
	if (!InnerGraph) return;
	for (UEdGraphNode* Node : InnerGraph->Nodes)
	{
		if (const UQuestlineNode_Exit* ExitNode = Cast<UQuestlineNode_Exit>(Node))
		{
			if (ExitNode->OutcomeTag.IsValid())
			{
				const FName PinName = ExitNode->OutcomeTag.GetTagName();
				if (!FindPin(PinName)) CreatePin(EGPD_Output, TEXT("QuestOutcome"), PinName);
			}
		}
	}
}

