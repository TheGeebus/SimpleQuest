// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class STableViewBase;

/**
 * Nomad-tab panel listing quest-component stale tag references across loaded editor worlds. Per-row Clear invokes
 * UQuestComponentBase::RemoveTags on the offending component; per-row Dismiss hides the row for the current editor
 * session (non-persistent). Read-only unless the designer explicitly clicks Clear.
 */
class SStaleQuestTagsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStaleQuestTagsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	using FEntryPtr = TSharedPtr<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>;

	/** Exposed so the row widget can bind its STextBlock::HighlightText attribute. */
	FText GetHighlightText() const { return FilterText; }
	
private:
	TSharedRef<ITableRow> HandleGenerateRow(FEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	EVisibility GetEmptyMessageVisibility() const;
	EVisibility GetListVisibility() const;
	FText GetStatusText() const;

	FReply HandleRefreshClicked();
	FReply HandleClearClicked(FEntryPtr Entry);
	FReply HandleDismissClicked(FEntryPtr Entry);
	FReply HandleFocusClicked(FEntryPtr Entry);

	void HandleFilterTextChanged(const FText& NewText);

	EColumnSortMode::Type GetColumnSortMode(FName ColumnId) const;
	void HandleSortColumn(EColumnSortPriority::Type SortPriority, const FName& ColumnId, EColumnSortMode::Type NewMode);

	void Refresh();
	
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

	/** Sort state — defaults to Actor ascending. */
	FName CurrentSortColumn;
	EColumnSortMode::Type CurrentSortMode = EColumnSortMode::Ascending;

	TSharedPtr<SListView<FEntryPtr>> ListView;
};

