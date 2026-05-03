// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"


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

    /** Current filter substring formatted for STextBlock::HighlightText. Public so row widgets can bind to it. */
    FText GetFilterTextAsText() const { return FText::FromString(FilterText); }

private:
    TSharedRef<ITableRow> HandleGenerateRow(TSharedPtr<FWorldStateFactRow> Item, const TSharedRef<STableViewBase>& OwnerTable);
    TSharedPtr<SWidget>   HandleContextMenuOpening();

    EVisibility GetEmptyMessageVisibility() const;
    FText GetEmptyMessageText() const;
    FText GetStatusText() const;

    /**
     * Subscribed to channel's OnSessionHistoryChanged — fires on session push/finalize and on every fact mutation
     * while in flight. Refreshes AllRows from the channel and requests list paint if the diff changed.
     */
    void HandleSessionHistoryChanged();

    /** Rebuilds the AllRows array from the channel's effective session. Returns true if the contents changed. */
    bool RefreshRowsFromChannel();

    /**
     * Returns the session index this view should display: SelectedSessionIndex if set to a valid index, else newest
     * session (auto-latest), else INDEX_NONE if the channel has no sessions. Phase 3 wires SelectedSessionIndex
     * to a dropdown; Phase 2c always uses INDEX_NONE so the view tracks newest.
     */
    int32 GetEffectiveSessionIndex() const;

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

    /**
     * Rebuilds SessionItems from the channel's history, refreshes the combo's options, and re-syncs the combo's
     * visually-selected item to GetEffectiveSessionIndex. Called from HandleSessionHistoryChanged.
     */
    void RebuildSessionItems();

    /** Generates the dropdown row widget for a session-item shared pointer (the wrapped int32 is the array index). */
    TSharedRef<SWidget> GenerateSessionItemWidget(TSharedPtr<int32> InItem);

    /**
     * Fires when the user picks a session via the combo. Writes PinnedSessionNumber and triggers a data refresh.
     * Programmatic SetSelectedItem calls (ESelectInfo::Direct) are ignored — only user picks change pin state.
     */
    void HandleSessionComboSelectionChanged(TSharedPtr<int32> NewItem, ESelectInfo::Type SelectInfo);

    /** Returns the label for a given session index — "Session N (in flight)" or "Session N (ended at t=X.XXs)". */
    FText MakeSessionLabel(int32 Index) const;

    /** Combo button's collapsed-state text — always reflects GetEffectiveSessionIndex via lambda. */
    FText GetSelectedSessionLabel() const;

    /**
     * SessionNumber the user has explicitly pinned via the combo. INDEX_NONE means "auto-track latest". Stored as
     * SessionNumber rather than array index so FIFO eviction in the channel doesn't silently shift the pin to a
     * different session. GetEffectiveSessionIndex resolves the SessionNumber to a current array index per call.
     */
    int32 PinnedSessionNumber = INDEX_NONE;

    /**
     * One TSharedPtr<int32> per session in history; the wrapped int32 is the array index at rebuild time. Rebuilt
     * on every OnSessionHistoryChanged so SComboBox's OptionsSource pointer stability holds across mutations.
     */
    TArray<TSharedPtr<int32>> SessionItems;

    TSharedPtr<SComboBox<TSharedPtr<int32>>> SessionCombo;

    FDelegateHandle SessionHistoryHandle;
};