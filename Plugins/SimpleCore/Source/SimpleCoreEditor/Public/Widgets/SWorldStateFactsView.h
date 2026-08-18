// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SColumnTableView.h"
#include "Widgets/SCompoundWidget.h"
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
    /**
     * Stable key for persisting this panel's table state - sort column, direction and column widths. Forwarded from
     * the hosting SFactsPanel via the FactsPanelRegistry factory signature, so two docked panels keep their own
     * layout instead of overwriting each other's. Empty disables persistence.
     */
    SLATE_ARGUMENT(FName, PanelPersistenceKey)
SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWorldStateFactsView();

private:
    /**
     * Column definitions for the table. Built once at construction - the columns carry the sorting, filtering and copy
     * behavior, so this is the only place that describes what a fact row looks like.
     */
    static TArray<FTableColumnDef<TSharedPtr<FWorldStateFactRow>>> MakeColumns();
    
    /** Every fact row for the effective session, unfiltered. The table owns filtering and sorting from here. */
    TArray<TSharedPtr<FWorldStateFactRow>> Rows;
    TSharedPtr<SColumnTableView<TSharedPtr<FWorldStateFactRow>>> Table;

    TSharedPtr<SWidget>   HandleContextMenuOpening();
    
    FText GetEmptyMessageText() const;
    FText GetStatusText() const;

    /**
     * Subscribed to channel's OnSessionHistoryChanged — fires on session push/finalize and on every fact mutation
     * while in flight. Refreshes the rows from the channel and hands them to the table if the diff changed.
     */
    void HandleSessionHistoryChanged();

    /** Rebuilds the row array from the channel's effective session. Returns true if the contents changed. */
    bool RefreshRowsFromChannel();

    /**
     * Returns the session index this view should display: SelectedSessionIndex if set to a valid index, else newest
     * session (auto-latest), else INDEX_NONE if the channel has no sessions. Phase 3 wires SelectedSessionIndex
     * to a dropdown; Phase 2c always uses INDEX_NONE so the view tracks newest.
     */
    int32 GetEffectiveSessionIndex() const;

    void CopySelectionAsTagList();
    void CopySelectionAsTagContainer();
    void CopySelectionAsTSV();

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