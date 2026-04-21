// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_LinkedQuestline.h"

#include "Quests/QuestlineGraph.h"
#include "Utilities/SimpleQuestEditorUtils.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

FText UQuestlineNode_LinkedQuestline::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// Palette / menu contexts — no instance exists yet, show the generic category label.
	if (TitleType == ENodeTitleType::MenuTitle || TitleType == ENodeTitleType::ListView)
	{
		return LOCTEXT("LinkedQuestlineTitlePrefix", "Linked Questline");
	}

	// Placed / editable contexts. Resolve a human-readable name via the standard fallback chain:
	//   FriendlyName (if set on the linked asset and the asset is resident), then
	//   asset short name (from the soft path, no load), then
	//   "(none)" (when LinkedGraph is null).
	// Avoids LoadSynchronous on paint: if the asset isn't already resident, we take the cheap short-name path.
	// The inline asset picker flow (item 3b, future session) keeps the asset resident on selection.
	FText Name;
	if (UQuestlineGraph* Resident = LinkedGraph.Get())
	{
		Name = Resident->GetDisplayName();
	}
	else if (!LinkedGraph.IsNull())
	{
		Name = FText::FromString(LinkedGraph.GetAssetName());
	}
	else
	{
		Name = LOCTEXT("LinkedQuestlineNone", "(none)");
	}

	return FText::Format(LOCTEXT("LinkedQuestlineTitleFmt", "Linked Questline - {0}"), Name);
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

void UQuestlineNode_LinkedQuestline::PostPlacedNewNode()
{
	// Run the base sweep (assigns QuestGuid, labels via the collision-avoiding candidate walk), then clear NodeLabel.
	// LinkedQuestline's identity is the referenced asset, not a per-instance label — the baseline "Node_N" fallback
	// ContentBase produces leaks into the title otherwise, since GetNodeTitle no longer fronts NodeLabel anywhere.
	Super::PostPlacedNewNode();
	NodeLabel = FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
