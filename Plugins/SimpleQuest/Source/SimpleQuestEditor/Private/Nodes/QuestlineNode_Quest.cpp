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

	// Deep-copy path. Paste serialized the source's inner graph along with its descendants — labels + topology
	// + pin connections are all carried through intact. Regenerate identity on every descendant content node so
	// they don't collide with the source's compiled tags. For nested Quest nodes, also re-wire subscriptions
	// and rebuild outcome pins (normally done inside CreateInnerGraph, which we're NOT calling here since we
	// want to keep the deep-copied content).
	if (InnerGraph)
	{
		InnerGraph->Modify();
		if (!InnerGraph->Schema)
		{
			InnerGraph->Schema = UQuestlineGraphSchema::StaticClass();
		}
		RegenerateInnerGraphIdentitiesRecursive(InnerGraph);

		SubscribeToInnerGraphChanges();
		RebuildOutcomePinsFromInnerGraph();
	}
	else
	{
		// Defensive fallback: serialization somehow didn't produce an inner graph. Create a fresh empty one.
		CreateInnerGraph();
	}
}

void UQuestlineNode_Quest::PostLoad()
{
	Super::PostLoad();
	SubscribeToInnerGraphChanges();
}

void UQuestlineNode_Quest::RegenerateInnerGraphIdentitiesRecursive(UEdGraph* Graph)
{
	if (!Graph) return;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		Node->Modify();
		Node->CreateNewGuid(); // UEdGraphNode::NodeGuid — separate from our compiler-level QuestGuid below.

		if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
		{
			ContentNode->QuestGuid = FGuid::NewGuid();
		}

		// Recurse into nested Quest's inner graph + re-wire subscription and outcome pins.
		if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
		{
			if (UEdGraph* NestedInner = QuestNode->GetInnerGraph())
			{
				NestedInner->Modify();
				if (!NestedInner->Schema)
				{
					NestedInner->Schema = UQuestlineGraphSchema::StaticClass();
				}
				RegenerateInnerGraphIdentitiesRecursive(NestedInner);

				// These are private on UQuestlineNode_Quest but accessible here because
				// RegenerateInnerGraphIdentitiesRecursive is a static member of the same class.
				QuestNode->SubscribeToInnerGraphChanges();
				QuestNode->RebuildOutcomePinsFromInnerGraph();
			}
		}
		// LinkedQuestline nodes inside the inner graph: QuestGuid is regenerated above via the ContentBase cast,
		// but we don't recurse — LinkedGraph is a soft-ref to an external asset we aren't duplicating.
	}
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

