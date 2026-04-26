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

	/**
	 * Per-actor metadata for every Clear action in this session. Keyed by actor weak ptr so the entries drop
	 * cleanly if an actor is destroyed; value carries the Source + PackagePath needed to re-emit fresh entries
	 * via FSimpleQuestEditorUtilities::ScanActorForStaleTags on PostUndo / PostRedo.
	 *
	 * Why per-actor (not per-FEntryPtr): undo for component property changes can invalidate the component's
	 * weak ptr without invalidating the actor's. The actor is the durable identity; the component is found by
	 * a fresh lookup at scan time. This sidesteps the post-undo identity-invalidation that broke the prior
	 * shadow-of-cleared-entries approach (where a single actor's three entries all shared a component weak
	 * ptr that went bad after undoing any one of them).
	 *
	 * BP CDO entries are deliberately not tracked here — their "actor" is a CDO, not a level instance, and
	 * BP CDO undo doesn't restore the underlying state regardless (known limitation).
	 */
	struct FActorScanInfo
	{
		FSimpleQuestEditorUtilities::EStaleQuestTagSource Source;
		FString PackagePath;
	};
	TMap<TWeakObjectPtr<AActor>, FActorScanInfo> ActorsTouchedByClear;

	int32 GetPendingSaveCount() const;
	FReply HandleSaveAllModifiedClicked();
	bool IsSaveAllModifiedEnabled() const;
	FText GetSaveAllModifiedLabel() const;
	
	/**
	 * Multi-select bulk Clear. Wraps every per-entry mutation in a single FScopedTransaction so Ctrl+Z restores
	 * the entire batch atomically. Confirmation dialog precedes the mutation; per-source dirty resolution +
	 * ModifiedPackages tracking matches the single-row Clear path. Disabled when no rows are selected.
	 */
	FReply HandleClearSelectedClicked();
	bool IsClearSelectedEnabled() const;
	FText GetClearSelectedLabel() const;

	FReply HandleScanLoadedClicked();      // Tier 1 only — scans loaded editor worlds
	FReply HandleFullScanClicked();     // Tier 1 + Tier 2 — adds BP CDOs + unloaded levels (slow-task wrapped)
	FReply HandleClearClicked(FEntryPtr Entry);
	FReply HandleFocusClicked(FEntryPtr Entry);

	/**
	 * Per-entry mutation core shared by HandleClearClicked (single) and HandleClearSelectedClicked (bulk).
	 * Calls RemoveTags on the component and resolves the dirtied UPackage* per Source — Loaded / Unloaded
	 * dirty their actor's package, BP CDO walks the outer chain to the UBlueprint and routes through
	 * MarkBlueprintAsModified. Returns nullptr if the entry's component is gc'd or no package was dirtied.
	 * Caller is responsible for the surrounding FScopedTransaction and for adding the returned package
	 * to ModifiedPackages. No row-list mutation — caller handles AllEntries removal + RebuildVisibleList.
	 */
	UPackage* ClearOneEntry(FEntryPtr Entry, int32& OutRemoved);
	
	/**
	 * Per-actor targeted rescan called from PostUndo / PostRedo. Walks ActorsTouchedByClear, re-runs
	 * ScanActorForStaleTags on each actor still alive, and replaces those actors' entries in AllEntries
	 * with fresh scan results. Sub-millisecond per actor; replaces the prior Refresh(strippedScope) call
	 * which was 1–2 seconds even after the BP CDO + comprehensive WP strip.
	 */
	void UpdateFromAffectedActors();

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

