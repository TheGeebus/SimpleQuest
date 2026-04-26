// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_LinkedQuestline.h"

#include "Quests/QuestlineGraph.h"
#include "Utilities/SimpleQuestEditorUtils.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

FText UQuestlineNode_LinkedQuestline::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// Palette / menu contexts show the type label only — no instance exists yet.
	if (TitleType == ENodeTitleType::MenuTitle || TitleType == ENodeTitleType::ListView)
	{
		return LOCTEXT("LinkedQuestlineMenuTitle", "Linked Questline");
	}

	// Placed / editable contexts show NodeLabel — the designer-authored identity that drives the compiled tag segment
	// (SimpleQuest.Quest.<ParentID>.<NodeLabel>). Same convention as Step and Quest. The linked asset reference is
	// displayed prominently in the inline picker widget on the node body, so no redundant header decoration.
	return NodeLabel;
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

	TArray<FName> DesiredOutcomes = FSimpleQuestEditorUtilities::CollectExitOutcomeTagNames(Graph);
	FSimpleQuestEditorUtilities::SortPinNamesAlphabetical(DesiredOutcomes);
	SyncPinsByCategory(EGPD_Output, TEXT("QuestOutcome"), DesiredOutcomes, { TEXT("QuestDeactivate"), TEXT("QuestDeactivated") });
}


#undef LOCTEXT_NAMESPACE
