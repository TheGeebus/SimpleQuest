// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Widgets/SQuestlineDebugPlacementWidget.h"

#include "Debug/QuestPIEDebugChannel.h"
#include "Quests/QuestlineGraph.h"
#include "SimpleQuestEditor.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

void SQuestlineDebugPlacementWidget::Construct(const FArguments& InArgs, UQuestlineGraph* InQuestlineGraph)
{
	QuestlineGraph = InQuestlineGraph;
	RebuildPlacements(/*bFromComboOpening=*/false);

	PlacementCombo = SNew(SComboBox<TSharedPtr<FQuestDebugPlacement>>)
		.ToolTipText(LOCTEXT("DebugPlacementTooltip",
			"While playing, choose which running placement of this questline the graph's debug overlay reports. Default is "
			"'All Placements', where every placement is reported at once - a node reads Live when any placement of it is."))
		.OptionsSource(&Placements)
		.InitiallySelectedItem(Placements.Num() > 0 ? Placements[0] : nullptr)
		.OnComboBoxOpening(this, &SQuestlineDebugPlacementWidget::RebuildPlacements, true)
		.OnSelectionChanged(this, &SQuestlineDebugPlacementWidget::HandleSelectionChanged)
		.OnGenerateWidget(this, &SQuestlineDebugPlacementWidget::MakeRowWidget)
		.ContentPadding(FMargin(0.f, 4.f))
		[
			SNew(STextBlock)
			.Text(this, &SQuestlineDebugPlacementWidget::GetSelectedLabel)
		];

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
		[
			PlacementCombo.ToSharedRef()
		]
	];
}

void SQuestlineDebugPlacementWidget::RebuildPlacements(bool bFromComboOpening)
{
	FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
	UQuestlineGraph* Asset = QuestlineGraph.Get();
	const FGameplayTag Selected = (Channel && Asset) ? Channel->GetDebugContextForAsset(Asset) : FGameplayTag();

	Placements.Reset();

	TSharedPtr<FQuestDebugPlacement> AllRow = MakeShared<FQuestDebugPlacement>();
	AllRow->Label = LOCTEXT("DebugPlacementAll", "All placements");
	AllRow->Tooltip = LOCTEXT("DebugPlacementAllTooltip",
		"Report every running placement at once - a node reads Live when any placement of it is.");
	Placements.Add(AllRow);

	TSharedPtr<FQuestDebugPlacement> ToSelect = AllRow;

	// Placements exist only while the game runs, and only for an asset something places - an asset running standalone, or
	// not running at all, offers the default alone. Rebuilt on open rather than ticked: the list only changes when the game
	// registers or drops a placement, and nobody can pick from a list they have not opened.
	if (Channel && Asset)
	{
		for (const FGameplayTag& PlacementTag : Channel->GetPlacementsForAsset(Asset))
		{
			TSharedPtr<FQuestDebugPlacement> Row = MakeShared<FQuestDebugPlacement>();
			Row->PlacementTag = PlacementTag;
			Row->Label = FSimpleQuestEditorUtilities::GetTagLeafLabel(PlacementTag.GetTagName());
			Row->Tooltip = FText::FromString(PlacementTag.ToString());
			Placements.Add(Row);

			if (PlacementTag == Selected) ToSelect = Row;
		}
	}

	if (PlacementCombo.IsValid())
	{
		PlacementCombo->RefreshOptions();
		PlacementCombo->SetSelectedItem(ToSelect);
	}
}

TSharedRef<SWidget> SQuestlineDebugPlacementWidget::MakeRowWidget(TSharedPtr<FQuestDebugPlacement> Item) const
{
	return SNew(STextBlock)
		.Text(Item.IsValid() ? Item->Label : FText::GetEmpty())
		.ToolTipText(Item.IsValid() ? Item->Tooltip : FText::GetEmpty());
}

void SQuestlineDebugPlacementWidget::HandleSelectionChanged(TSharedPtr<FQuestDebugPlacement> NewSelection, ESelectInfo::Type SelectInfo)
{
	FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
	UQuestlineGraph* Asset = QuestlineGraph.Get();
	if (!Channel || !Asset || !NewSelection.IsValid()) return;

	// RebuildPlacements re-selects the current row every time the list opens, which arrives here as a selection change.
	// Writing the same answer back would be harmless but would log and broadcast on every open, so say nothing when
	// nothing changed.
	if (Channel->GetDebugContextForAsset(Asset) == NewSelection->PlacementTag) return;

	Channel->SetDebugContextForAsset(Asset, NewSelection->PlacementTag);
}

FText SQuestlineDebugPlacementWidget::GetSelectedLabel() const
{
	FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
	UQuestlineGraph* Asset = QuestlineGraph.Get();
	if (!Channel || !Asset) return LOCTEXT("DebugPlacementNone", "Debug - All placements shown");

	const FGameplayTag Selected = Channel->GetDebugContextForAsset(Asset);
	return Selected.IsValid()
		? FSimpleQuestEditorUtilities::GetTagLeafLabel(Selected.GetTagName())
		: LOCTEXT("DebugPlacementNone", "Debug - All placements shown");
}

#undef LOCTEXT_NAMESPACE

