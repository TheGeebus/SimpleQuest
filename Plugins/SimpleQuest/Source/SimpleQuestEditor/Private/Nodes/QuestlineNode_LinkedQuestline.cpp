// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_LinkedQuestline.h"

#include "Quests/QuestlineGraph.h"
#include "Utilities/SimpleQuestEditorUtils.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

FText UQuestlineNode_LinkedQuestline::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	/**
	 * Menu/list views: simple "Linked Questline" category label — no instance exists yet so NodeLabel is not meaningful.
	 */
	if (TitleType == ENodeTitleType::MenuTitle || TitleType == ENodeTitleType::ListView)
	{
		return LOCTEXT("LinkedQuestlineTitlePrefix", "Linked Questline");
	}

	/**
	 * Inline rename (EditableTitle): NodeLabel alone so the rename field pre-populates with only the designer-facing
	 * portion and typing replaces just that, not the full multi-line title.
	 */
	if (TitleType == ENodeTitleType::EditableTitle)
	{
		return NodeLabel;
	}

	/**
	 * Full/default titles on a placed instance: NodeLabel as the primary line, asset name as a secondary line. Designers
	 * distinguish multiple placements of the same asset via inline rename and the change is immediately visible in the
	 * graph. When the inline asset picker widget (Session 22 polish) lands, the asset line drops since the picker will
	 * render the asset inline on the node body.
	 */
	FText AssetNameText;
	if (!LinkedGraph.IsNull())
	{
		AssetNameText = FText::FromString(LinkedGraph.GetAssetName());
	}
	else
	{
		AssetNameText = LOCTEXT("LinkedQuestlineNone", "(none)");
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("Label"), NodeLabel);
	Args.Add(TEXT("Asset"), AssetNameText);
	return FText::Format(LOCTEXT("LinkedQuestlineTitleWithLabelFmt", "{Label}\nLinked Questline: {Asset}"), Args);
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
