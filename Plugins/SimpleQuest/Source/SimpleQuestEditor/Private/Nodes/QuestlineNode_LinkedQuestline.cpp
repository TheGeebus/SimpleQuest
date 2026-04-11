// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_LinkedQuestline.h"

#include "Quests/QuestlineGraph.h"
#include "Utilities/SimpleQuestEditorUtils.h"

FText UQuestlineNode_LinkedQuestline::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!LinkedGraph.IsNull()) return FText::FromString(LinkedGraph.GetAssetName());
	if (!NodeLabel.IsEmpty()) return NodeLabel;
	return NSLOCTEXT("SimpleQuestEditor", "LinkedNodeDefaultTitle", "Linked Questline");
}

FLinearColor UQuestlineNode_LinkedQuestline::GetNodeTitleColor() const
{
	return SQ_ED_NODE_LINKED;
}

void UQuestlineNode_LinkedQuestline::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_LinkedQuestline, LinkedGraph))
	{
		RebuildOutcomePinsFromLinkedGraph();
	}
}

void UQuestlineNode_LinkedQuestline::PostLoad()
{
	Super::PostLoad();
	RebuildOutcomePinsFromLinkedGraph();
}

void UQuestlineNode_LinkedQuestline::RebuildOutcomePinsFromLinkedGraph()
{
	UEdGraph* Graph = nullptr;
	if (!LinkedGraph.IsNull())
	{
		if (UQuestlineGraph* LoadedGraph = LinkedGraph.LoadSynchronous())
		{
			Graph = LoadedGraph->QuestlineEdGraph;
		}
	}

	const TArray<FName> DesiredOutcomes = USimpleQuestEditorUtilities::CollectExitOutcomeTagNames(Graph);
	SyncPinsByCategory(EGPD_Output, TEXT("QuestOutcome"), DesiredOutcomes, { TEXT("QuestDeactivate"), TEXT("QuestDeactivated") });
}
