// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "EditorUndoClient.h"

class ITableRow;
class STableViewBase;

/**
 * Nomad-tab panel listing quest-component stale tag references across loaded editor worlds. Per-row Clear invokes
 * UQuestComponentBase::RemoveTags on the offending component; per-row Dismiss hides the row for the current editor
 * session (non-persistent). Read-only unless the designer explicitly clicks Clear.
 */
class SStaleQuestTagsPanel : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SStaleQuestTagsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SStaleQuestTagsPanel();

	using FEntryPtr = TSharedPtr<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>;

	/** Exposed so the row widget can bind its STextBlock::HighlightText attribute. */
	FText GetHighlightText() const { return FilterText; }

	// FEditorUndoClient interface — GEditor fires these after any undo/redo so the panel re-scans and
	// any rows restored by Ctrl+Z reappear (or re-hide on Ctrl+Y).
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	
private:
	TSharedRef<ITableRow> HandleGenerateRow(FEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	EVisibility GetEmptyMessageVisibility() const;
	EVisibility GetListVisibility() const;
	FText GetStatusText() const;

	/**
	 * Packages dirtied by per-row Clear actions, awaiting designer save. Tracked so the panel can surface
	 * pending count + offer a "Save All Modified" button. Weak ptrs so unloaded packages drop cleanly.
	 */
	TSet<TWeakObjectPtr<UPackage>> ModifiedPackages;

	int32 GetPendingSaveCount() const;
	FReply HandleSaveAllModifiedClicked();
	bool IsSaveAllModifiedEnabled() const;
	FText GetSaveAllModifiedLabel() const;

	FReply HandleRefreshClicked();      // Tier 1 only — scans loaded editor worlds
	FReply HandleFullScanClicked();     // Tier 1 + Tier 2 — adds BP CDOs + unloaded levels (slow-task wrapped)
	FReply HandleClearClicked(FEntryPtr Entry);
	FReply HandleFocusClicked(FEntryPtr Entry);

	void HandleFilterTextChanged(const FText& NewText);

	EColumnSortMode::Type GetColumnSortMode(FName ColumnId) const;
	void HandleSortColumn(EColumnSortPriority::Type SortPriority, const FName& ColumnId, EColumnSortMode::Type NewMode);

	void Refresh(FSimpleQuestEditorUtilities::FStaleTagScanScope Scope = FSimpleQuestEditorUtilities::FStaleTagScanScope());
	
	/**
	 * Rebuilds VisibleEntries from AllEntries applying: dismiss filter, text filter, sort. Single entry point for any
	 * state change that affects the visible list.
	 */
	void RebuildVisibleList();

	/** Full scan result. Rebuilt on Refresh. */
	TArray<FEntryPtr> AllEntries;
	/** Visible rows after dismiss + text filter + sort. */
	TArray<FEntryPtr> VisibleEntries;

	/** Current filter text — also serves as the STextBlock highlight source. */
	FText FilterText;

	/**
	 * Scope of the most recent Refresh / Full Project Scan invocation. PostUndo / PostRedo re-run with this
	 * scope so undo restores the entry to the same view the designer was looking at, rather than narrowing
	 * to the panel's default (Tier 1 only) and silently dropping any Tier 2 rows the designer had pulled in
	 * via a prior Full Project Scan. Updated by Refresh; initialized to default (Tier 1) at Construct.
	 */
	FSimpleQuestEditorUtilities::FStaleTagScanScope LastScope;

	/** Sort state — defaults to Actor ascending. */
	FName CurrentSortColumn;
	EColumnSortMode::Type CurrentSortMode = EColumnSortMode::Ascending;

	TSharedPtr<SListView<FEntryPtr>> ListView;
};

