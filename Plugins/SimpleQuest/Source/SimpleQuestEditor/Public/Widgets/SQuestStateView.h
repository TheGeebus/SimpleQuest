// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"


class ITableRow;
class STableViewBase;

/**
 * Active tab inside the Quest State view. Switches the visible list among the three datasets that
 * UQuestStateSubsystem maintains. Per-tab state (filter text matches all tabs; sort column/mode is per-tab)
 * is preserved across tab switches.
 */
enum class EQuestStateViewTab : uint8
{
    Resolutions,
    Entries,
    PrereqStatus,
};

/** Row data for the Resolutions tab — one row per FQuestResolutionEntry across all quests. */
struct FQuestStateResolutionRow
{
    FGameplayTag QuestTag;
    FGameplayTag OutcomeTag;
    double       ResolutionTime = 0.0;
    EQuestResolutionSource Source = EQuestResolutionSource::Graph;
};

/** Row data for the Entries tab — one row per FQuestEntryArrival across all destination quests. */
struct FQuestStateEntryRow
{
    FGameplayTag DestTag;
    FGameplayTag SourceQuestTag;
    FGameplayTag IncomingOutcomeTag;
    double       EntryTime = 0.0;
};

/** Row data for the Prereq Status tab — one row per quest currently in PendingGiver state with a cached snapshot. */
struct FQuestStatePrereqRow
{
    FGameplayTag QuestTag;
    bool         bIsAlways = false;
    bool         bSatisfied = false;
    int32        UnsatisfiedLeafCount = 0;
};

/**
 * Hosted view (inside SFactsPanel) that lists live UQuestStateSubsystem registry contents during PIE.
 * Three tabs — Resolutions / Entries / Prereq Status — each surfacing one of the registry maps as a
 * sortable, filterable, copy-able table. Mirrors the SWorldStateFactsView pattern (tick-based snapshot
 * diff, multi-select rows, alternating row tint, right-click clipboard copy, substring filter highlight)
 * per tab. Filter text is shared across tabs; sort state is per-tab.
 */
class SIMPLEQUESTEDITOR_API SQuestStateView : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SQuestStateView) {}
        /** Optional: stable key for persisting per-instance state (active sub-tab) to GEditorPerProjectIni.
         *  Forwarded from the hosting SFactsPanel via the FactsPanelRegistry factory signature. Empty key
         *  disables persistence — view defaults to Resolutions tab each construction. */
        SLATE_ARGUMENT(FName, PersistenceKey)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SQuestStateView();

    /** Current filter substring formatted for STextBlock::HighlightText. Public so row widgets can bind to it. */
    FText GetFilterTextAsText() const { return FText::FromString(FilterText); }

private:
    // ── Lifecycle / refresh ──────────────────────────────────────────────────────────────────────────
    
    /**
     * Subscribed to channel's OnSessionHistoryChanged — fires on session push/finalize and on every registry mutation
     * while in flight. Refreshes all three tabs from the channel and requests list paint per tab if the diff changed.
     */
    void HandleSessionHistoryChanged();

    /**
     * Returns the session index this view should display: SelectedSessionIndex if set to a valid index, else newest
     * session (auto-latest), else INDEX_NONE.
     */
    int32 GetEffectiveSessionIndex() const;
    

    // ── Tab switching ────────────────────────────────────────────────────────────────────────────────
    void HandleTabChanged(EQuestStateViewTab NewTab);
    EVisibility GetTabVisibility(EQuestStateViewTab Tab) const;
    bool        IsTabActive(EQuestStateViewTab Tab) const { return ActiveTab == Tab; }

    // ── Status / empty messaging ─────────────────────────────────────────────────────────────────────
    EVisibility GetEmptyMessageVisibility() const;
    EVisibility GetListVisibility() const;
    FText       GetEmptyMessageText() const;
    FText       GetStatusText() const;

    // ── Filter ───────────────────────────────────────────────────────────────────────────────────────
    void HandleFilterTextChanged(const FText& NewText);
    void ApplyAllFilters();

    // ── Per-tab refresh (returns true if contents changed) ────────────────────────────────────────────
    bool RefreshResolutionsFromChannel();
    bool RefreshEntriesFromChannel();
    bool RefreshPrereqsFromChannel();

    // ── Per-tab sort ─────────────────────────────────────────────────────────────────────────────────
    void SortResolutions();
    void SortEntries();
    void SortPrereqs();

    // ── Per-tab filter (rebuilds Rows from AllRows applying the filter substring) ────────────────────
    void ApplyResolutionsFilter();
    void ApplyEntriesFilter();
    void ApplyPrereqsFilter();

    // ── Per-tab row generation ───────────────────────────────────────────────────────────────────────
    TSharedRef<ITableRow> HandleGenerateResolutionRow(TSharedPtr<FQuestStateResolutionRow> Item, const TSharedRef<STableViewBase>& OwnerTable);
    TSharedRef<ITableRow> HandleGenerateEntryRow(TSharedPtr<FQuestStateEntryRow> Item, const TSharedRef<STableViewBase>& OwnerTable);
    TSharedRef<ITableRow> HandleGeneratePrereqRow(TSharedPtr<FQuestStatePrereqRow> Item, const TSharedRef<STableViewBase>& OwnerTable);

    // ── Context menu / copy ──────────────────────────────────────────────────────────────────────────
    TSharedPtr<SWidget> HandleContextMenuOpening();
    /** Copies the cell-specific tag captured by the most recent right-click. Bound by HandleContextMenuOpening. */
    void CopyRightClickedTag(FGameplayTag InTag);
    void CopySelectedRowsAsTSV();

    // ── Per-tab sort wiring ──────────────────────────────────────────────────────────────────────────
    EColumnSortMode::Type GetResolutionsSortMode(FName ColumnID) const;
    EColumnSortMode::Type GetEntriesSortMode(FName ColumnID) const;
    EColumnSortMode::Type GetPrereqsSortMode(FName ColumnID) const;
    void HandleResolutionsColumnSort(EColumnSortPriority::Type, const FName& ColumnID, EColumnSortMode::Type NewMode);
    void HandleEntriesColumnSort(EColumnSortPriority::Type, const FName& ColumnID, EColumnSortMode::Type NewMode);
    void HandlePrereqsColumnSort(EColumnSortPriority::Type, const FName& ColumnID, EColumnSortMode::Type NewMode);

    // ── State ────────────────────────────────────────────────────────────────────────────────────────
    EQuestStateViewTab ActiveTab = EQuestStateViewTab::Resolutions;
    FString FilterText;
    
    // Cell-aware right-click capture. Each tag-typed cell wraps its widget in an SBorder with an
    // OnMouseButtonDown handler that calls NotifyTagRightClicked → which sets these. HandleContextMenu-
    // Opening reads bLastRightClickedTagSet to decide whether to surface a "Copy '<tag>' Tag" entry, then
    // clears the flag so stale state from a previous right-click doesn't leak into a non-tag-cell click.
    FGameplayTag LastRightClickedTag;
    bool bLastRightClickedTagSet = false;
    
    void NotifyTagRightClicked(const FGameplayTag& InTag) { LastRightClickedTag = InTag; bLastRightClickedTagSet = true; }

    TArray<TSharedPtr<FQuestStateResolutionRow>> AllResolutions;
    TArray<TSharedPtr<FQuestStateResolutionRow>> Resolutions;
    TSharedPtr<SListView<TSharedPtr<FQuestStateResolutionRow>>> ResolutionsList;

    TArray<TSharedPtr<FQuestStateEntryRow>> AllEntries;
    TArray<TSharedPtr<FQuestStateEntryRow>> Entries;
    TSharedPtr<SListView<TSharedPtr<FQuestStateEntryRow>>> EntriesList;

    TArray<TSharedPtr<FQuestStatePrereqRow>> AllPrereqs;
    TArray<TSharedPtr<FQuestStatePrereqRow>> Prereqs;
    TSharedPtr<SListView<TSharedPtr<FQuestStatePrereqRow>>> PrereqsList;

    FName ResolutionsSortColumn;
    EColumnSortMode::Type ResolutionsSortMode = EColumnSortMode::None;
    FName EntriesSortColumn;
    EColumnSortMode::Type EntriesSortMode = EColumnSortMode::None;
    FName PrereqsSortColumn;
    EColumnSortMode::Type PrereqsSortMode = EColumnSortMode::None;

    /** Rebuilds SessionItems from the channel's history, refreshes the combo, re-syncs visually-selected item. */
    void RebuildSessionItems();

    /** Generates the dropdown row widget for a session item (wrapped int32 = array index at rebuild time). */
    TSharedRef<SWidget> GenerateSessionItemWidget(TSharedPtr<int32> InItem);

    /** User-driven combo selection. ESelectInfo::Direct skipped (programmatic re-sync). */
    void HandleSessionComboSelectionChanged(TSharedPtr<int32> NewItem, ESelectInfo::Type SelectInfo);

    /** Session label per spec — "Session N (in flight)" or "Session N (ended at t=X.XXs)". */
    FText MakeSessionLabel(int32 Index) const;

    /** Combo button's collapsed-state text — always reflects GetEffectiveSessionIndex via lambda. */
    FText GetSelectedSessionLabel() const;

    /**
     * SessionNumber the user has explicitly pinned via the combo. INDEX_NONE means "auto-track latest". Stored as
     * SessionNumber rather than array index so FIFO eviction in the channel doesn't silently shift the pin.
     */
    int32 PinnedSessionNumber = INDEX_NONE;

    /** One TSharedPtr<int32> per session in history. Rebuilt on every OnSessionHistoryChanged. */
    TArray<TSharedPtr<int32>> SessionItems;

    TSharedPtr<SComboBox<TSharedPtr<int32>>> SessionCombo;

    FDelegateHandle SessionHistoryHandle;

    /** Persistence key forwarded by the hosting SFactsPanel; empty when persistence is disabled. */
    FName PersistenceKey;
};