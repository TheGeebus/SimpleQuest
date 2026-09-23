// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class UQuestlineGraph;

/** One row of the placement picker: a running placement of the opened asset, or the "all placements" default. */
struct FQuestDebugPlacement
{
	/** Invalid on the "All placements" row, which is how the debug channel spells "report every placement at once". */
	FGameplayTag PlacementTag;

	FText Label;
	FText Tooltip;
};

/**
 * Toolbar picker for which running placement of the opened questline the graph's debug overlay reports.
 *
 * An asset placed more than once has one authored node per Step and several running instances of it, and every fact is
 * written at every perspective - so a Step's own asset-level address belongs to all of them and the overlay, left alone,
 * reports the placements merged. This chooses one. Modelled on the Blueprint editor's debug object picker, and the same
 * shape: the current choice on the button, the alternatives in the list, the list rebuilt when the list is opened.
 */
class SQuestlineDebugPlacementWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestlineDebugPlacementWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UQuestlineGraph* InQuestlineGraph);

private:
	/** Rebuilds the row list from the placements registered right now and re-selects whatever the channel currently holds. */
	void RebuildPlacements(bool bFromComboOpening);

	TSharedRef<SWidget> MakeRowWidget(TSharedPtr<FQuestDebugPlacement> Item) const;
	void HandleSelectionChanged(TSharedPtr<FQuestDebugPlacement> NewSelection, ESelectInfo::Type SelectInfo);

	/** The button's text. Reads the channel rather than the combo's own selection, so it is right the instant PIE ends. */
	FText GetSelectedLabel() const;

	TWeakObjectPtr<UQuestlineGraph> QuestlineGraph;

	TArray<TSharedPtr<FQuestDebugPlacement>> Placements;

	TSharedPtr<SComboBox<TSharedPtr<FQuestDebugPlacement>>> PlacementCombo;
};

