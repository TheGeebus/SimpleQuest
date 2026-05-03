// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
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
 * Hosted view (inside SFactsPanel) that lists live UWorldStateSubsystem facts during PIE. Two columns: Tag and Count -
 * with click-to-sort headers, multi-select rows, alternating row tint, right-click context menu for clipboard copy
 * (single-tag list / FGameplayTagContainer text-export format), and live substring highlighting on the active filter.
 *
 * Refresh strategy: full rebuild on PIE-active transitions, per-tick diff poll while active. Editing is not supported.
 */
class SWorldStateFactsView : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWorldStateFactsView) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWorldStateFactsView();

    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

    /** Current filter substring formatted for STextBlock::HighlightText. Public so row widgets can bind to it. */
    FText GetFilterTextAsText() const { return FText::FromString(FilterText); }

private:
    TSharedRef<ITableRow> HandleGenerateRow(TSharedPtr<FWorldStateFactRow> Item, const TSharedRef<STableViewBase>& OwnerTable);
    TSharedPtr<SWidget>   HandleContextMenuOpening();

    EVisibility GetEmptyMessageVisibility() const;
    FText GetEmptyMessageText() const;
    FText GetStatusText() const;

    void HandleDebugActiveChanged();
    void RebuildRows();

    /** Rebuilds the AllRows array from the channel's current facts. Returns true if the contents changed. */
    bool RefreshRowsFromChannel();

    /** Reorders AllRows in place per CurrentSortColumn / CurrentSortMode. No-op when CurrentSortMode is None. */
    void SortAllRows();

    void HandleFilterTextChanged(const FText& NewText);
    void ApplyFilter();

    EColumnSortMode::Type GetColumnSortMode(FName ColumnID) const;
    void HandleColumnSort(EColumnSortPriority::Type SortPriority, const FName& ColumnID, EColumnSortMode::Type NewSortMode);

    void CopySelectionAsTagList();
    void CopySelectionAsTagContainer();
    void CopySelectionAsTSV();

    /** Full set of rows pulled from WorldState (pre-filter). Rebuilt on PIE transitions and per-tick fact changes. */
    TArray<TSharedPtr<FWorldStateFactRow>> AllRows;
    FString FilterText;

    TArray<TSharedPtr<FWorldStateFactRow>> Rows;
    TSharedPtr<SListView<TSharedPtr<FWorldStateFactRow>>> ListView;

    FName CurrentSortColumn;
    EColumnSortMode::Type CurrentSortMode = EColumnSortMode::None;

    FDelegateHandle DebugActiveHandle;
};