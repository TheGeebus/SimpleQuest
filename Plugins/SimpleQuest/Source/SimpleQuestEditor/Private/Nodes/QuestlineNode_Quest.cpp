// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Nodes/QuestlineNode_Quest.h"

#include "SimpleQuestLog.h"
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

	// DUPLICATION ALREADY BROUGHT THE INNER GRAPH. InnerGraph is a UPROPERTY outered to this node, so the copy that the
	// Content Browser makes carries it and everything inside it across intact. Creating a fresh one here would discard
	// that copy and hand the duplicate an empty container - which is what a designer sees as "I duplicated the asset and
	// all my quests are gone".
	AdoptCopiedInnerGraph(/*bClearImportKeys*/ false);
}

void UQuestlineNode_Quest::PostPasteNode()
{
	Super::PostPasteNode();

	// Paste serialized the source's inner graph along with its descendants - labels, topology and pin connections all
	// carried through. Same adoption as duplication, except the import provenance is dropped: see AdoptCopiedInnerGraph.
	AdoptCopiedInnerGraph(/*bClearImportKeys*/ true);
}

void UQuestlineNode_Quest::AdoptCopiedInnerGraph(bool bClearImportKeys)
{
	if (!InnerGraph)
	{
		// Defensive: the copy somehow produced no inner graph. An empty container is still better than a null one.
		UE_LOG(LogSimpleQuest, Warning, TEXT("Quest node '%s' was copied without an inner graph; creating an empty one."),
			   *GetName());
		CreateInnerGraph();
		return;
	}

	InnerGraph->Modify();
	if (!InnerGraph->Schema)
	{
		InnerGraph->Schema = UQuestlineGraphSchema::StaticClass();
	}

	// Identity is reissued unconditionally, including on the duplication path where the engine also calls PostDuplicate
	// on each copied inner node itself. Reissuing a GUID nothing has referenced yet is free, and depending on the engine
	// to walk into a nested subobject is a bet this does not need to take.
	RegenerateInnerGraphIdentitiesRecursive(InnerGraph, bClearImportKeys);

	SubscribeToInnerGraphChanges();
	RebuildOutcomePinsFromInnerGraph();

	UE_LOG(LogSimpleQuest, Verbose, TEXT("Quest node '%s' adopted a copied inner graph: %d node(s), import keys %s."),
		   *GetName(),
		   InnerGraph->Nodes.Num(),
		   bClearImportKeys ? TEXT("cleared") : TEXT("kept"));
}

void UQuestlineNode_Quest::PostLoad()
{
	Super::PostLoad();
	SubscribeToInnerGraphChanges();
}

void UQuestlineNode_Quest::RegenerateInnerGraphIdentitiesRecursive(UEdGraph* Graph, bool bClearImportKeys)
{
	if (!Graph) return;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		Node->Modify();
		Node->CreateNewGuid(); // UEdGraphNode::NodeGuid - separate from our compiler-level QuestGuid below.

		// QuestGuid lives on UQuestlineNodeBase, and the base class reissues it for EVERY node kind on paste and on
		// duplicate. Narrowing this walk to content nodes would leave a group or utility node inside a copied container
		// carrying its source's identity, which is the one thing this walk exists to prevent.
		if (UQuestlineNodeBase* QuestlineNode = Cast<UQuestlineNodeBase>(Node))
		{
			QuestlineNode->QuestGuid = FGuid::NewGuid();
			if (bClearImportKeys) { QuestlineNode->ImportSourceKey.Reset(); }
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
				RegenerateInnerGraphIdentitiesRecursive(NestedInner, bClearImportKeys);

				// These are private on UQuestlineNode_Quest but accessible here because
				// RegenerateInnerGraphIdentitiesRecursive is a static member of the same class.
				QuestNode->SubscribeToInnerGraphChanges();
				QuestNode->RebuildOutcomePinsFromInnerGraph();
			}
		}
		// LinkedQuestline nodes inside the inner graph: QuestGuid is regenerated above, but we don't recurse -
		// LinkedGraph is a soft-ref to an external asset we aren't duplicating.
	}
}

void UQuestlineNode_Quest::NotifyInnerGraphsOfRename()
{
	FSimpleQuestEditorUtilities::NotifyGraphAndDescendants(GetInnerGraph());
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

