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

void UQuestlineNode_Quest::PostPasteNode()
{
	Super::PostPasteNode();
	// Paste deserialized the source's inner graph (deep copy, complete with source's inner-node QuestGuids).
	// PostPasteNode doesn't cascade to child-graph nodes in SGraphEditor's paste flow, so those inner nodes would
	// keep duplicate identities. Discard the deserialized inner graph and create a fresh empty one — matches the
	// "duplicate = fresh empty Quest" design choice already encoded in PostDuplicate.
	CreateInnerGraph();
}

void UQuestlineNode_Quest::PostLoad()
{
	Super::PostLoad();
	SubscribeToInnerGraphChanges();
}

static void NotifyGraphAndDescendants(UEdGraph* Graph)
{
	if (!Graph) return;
	Graph->NotifyGraphChanged();
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
		{
			if (UEdGraph* InnerGraph = QuestNode->GetInnerGraph())
			{
				NotifyGraphAndDescendants(InnerGraph);
			}
		}
	}
}

void UQuestlineNode_Quest::NotifyInnerGraphsOfRename()
{
	NotifyGraphAndDescendants(GetInnerGraph());
}

void UQuestlineNode_Quest::CreateInnerGraph()
{
	InnerGraph = NewObject<UEdGraph>(this, UEdGraph::StaticClass(), NAME_None, RF_Transactional);
	InnerGraph->Schema = UQuestlineGraphSchema::StaticClass();
	const UQuestlineGraphSchema* Schema = GetDefault<UQuestlineGraphSchema>();
	Schema->CreateDefaultNodesForGraph(*InnerGraph);
	SubscribeToInnerGraphChanges();
}

void UQuestlineNode_Quest::SubscribeToInnerGraphChanges()
{
	if (InnerGraph && !InnerGraphChangedHandle.IsValid())
	{
		InnerGraphChangedHandle = InnerGraph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateUObject(this, &UQuestlineNode_Quest::OnInnerGraphChanged));
	}
}

void UQuestlineNode_Quest::OnInnerGraphChanged(const FEdGraphEditAction& Action)
{
	RebuildOutcomePinsFromInnerGraph();
}

void UQuestlineNode_Quest::RebuildOutcomePinsFromInnerGraph()
{
	TArray<FName> DesiredOutcomes = FSimpleQuestEditorUtilities::CollectExitOutcomeTagNames(InnerGraph);
	FSimpleQuestEditorUtilities::SortPinNamesAlphabetical(DesiredOutcomes);
	SyncPinsByCategory(EGPD_Output, TEXT("QuestOutcome"), DesiredOutcomes, { TEXT("QuestDeactivate"), TEXT("QuestDeactivated") });
}

