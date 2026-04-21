// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class STableViewBase;

/** Row data: one fact = one tag + its current assertion count. */

struct FWorldStateFactRow
{
	FGameplayTag Tag;
	int32        Count = 0;

	FWorldStateFactRow() = default;
	FWorldStateFactRow(const FGameplayTag InTag, const int32 InCount) : Tag(InTag), Count(InCount) {}
};

/**
 * Nomad-tab panel listing live UWorldStateSubsystem facts during PIE. Two columns — Tag and Count — sorted
 * alphabetically by tag. Refresh strategy: full rebuild on PIE-active transitions, per-tick diff poll while active.
 * Editing is not supported (panel is read-only).
 */
class SWorldStateFactsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWorldStateFactsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SWorldStateFactsPanel();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<ITableRow> HandleGenerateRow(TSharedPtr<FWorldStateFactRow> Item, const TSharedRef<STableViewBase>& OwnerTable);
	EVisibility GetEmptyMessageVisibility() const;
	EVisibility GetListVisibility() const;
	FText GetEmptyMessageText() const;
	FText GetStatusText() const;

	void HandleDebugActiveChanged();
	void RebuildRows();

	/** Rebuilds the Rows array from the channel's current facts. Returns true if the contents changed. */
	bool RefreshRowsFromChannel();

	void HandleFilterTextChanged(const FText& NewText);
	void ApplyFilter();

	/** Full set of rows pulled from WorldState (pre-filter). Rebuilt on PIE transitions + per-tick fact changes. */
	TArray<TSharedPtr<FWorldStateFactRow>> AllRows;
	FString FilterText;
	
	TArray<TSharedPtr<FWorldStateFactRow>> Rows;
	TSharedPtr<SListView<TSharedPtr<FWorldStateFactRow>>> ListView;

	FDelegateHandle DebugActiveHandle;
};
