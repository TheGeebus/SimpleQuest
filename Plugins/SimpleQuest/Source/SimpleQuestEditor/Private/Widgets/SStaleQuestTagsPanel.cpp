// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SStaleQuestTagsPanel.h"

#include "EngineUtils.h"
#include "Components/QuestComponentBase.h"
#include "SimpleQuestLog.h"
#include "ScopedTransaction.h"
#include "Engine/Blueprint.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "FileHelpers.h"
#include "Components/QuestGiverComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "SStaleQuestTagsPanel"

namespace
{
	const FName Col_Find = "Find";
	const FName Col_Source = "Source";
	const FName Col_Level = "Level";
	const FName Col_Actor = "Actor";
	const FName Col_Comp = "Component";
	const FName Col_Field = "Field";
	const FName Col_Tag = "StaleTag";
	const FName Col_Actions = "Actions";

	/**
	 * Short, sortable text label for the Source column. Also used by the filter so designers can type
	 * "Open", "BP CDO", or "Unloaded" in the search box to narrow rows by source. "Open" is used (rather
	 * than "Loaded") so the label doesn't substring-overlap with "Unloaded" — typing "loaded" wouldn't
	 * cleanly distinguish the two states otherwise. "Open" also tracks UE's File → Open Level vocabulary.
	 */
	FString SourceLabel(FSimpleQuestEditorUtilities::EStaleQuestTagSource Source)
	{
		using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;
		switch (Source)
		{
		case EStaleQuestTagSource::LoadedLevelInstance: return TEXT("Open");
		case EStaleQuestTagSource::ActorBlueprintCDO: return TEXT("BP CDO");
		case EStaleQuestTagSource::UnloadedLevelInstance: return TEXT("Unloaded");
		default: return TEXT("?");
		}
	}
}

class SStaleQuestTagRow : public SMultiColumnTableRow<SStaleQuestTagsPanel::FEntryPtr>
{
public:
	SLATE_BEGIN_ARGS(SStaleQuestTagRow) {}
		SLATE_ARGUMENT(SStaleQuestTagsPanel::FEntryPtr, Entry)
		SLATE_ATTRIBUTE(FText, HighlightText)
		SLATE_EVENT(FOnClicked, OnFocusClicked)
		SLATE_EVENT(FOnClicked, OnClearClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Entry = InArgs._Entry;
		HighlightText = InArgs._HighlightText;
		OnFocusClicked = InArgs._OnFocusClicked;
		OnClearClicked = InArgs._OnClearClicked;
		SMultiColumnTableRow<SStaleQuestTagsPanel::FEntryPtr>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		// Zebra stripe per-cell.
		auto WithStripe = [this](const TSharedRef<SWidget>& Inner) -> TSharedRef<SWidget>
		{
			return SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(this, &SStaleQuestTagRow::GetStripeColor)
				.Padding(0)
				[ Inner ];
		};

		auto TextCell = [this](const FString& Str) -> TSharedRef<SWidget>
		{
			return SNew(SBox).VAlign(VAlign_Center).Padding(FMargin(4.f, 0.f))
				[
					SNew(STextBlock)
						.Text(FText::FromString(Str))
						.HighlightText(HighlightText)
				];
		};

		using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;

		if (ColumnName == Col_Find)
		{
			// Source-aware icon + tooltip. Computed at row construction; refreshed when the entry's
			// Source field changes, via the fresh-TSharedPtr swap in Refresh's morph step (which
			// invalidates the row's widget cache and forces regeneration). Cheaper than lambda-based
			// reactive bindings since it doesn't re-evaluate every paint.
			const FSlateBrush* IconBrush = FAppStyle::Get().GetBrush("Icons.Search");
			FText Tooltip = LOCTEXT("FocusTooltip_Loaded", "Select this actor in its level and frame the viewport on it.");
			if (Entry.IsValid())
			{
				switch (Entry->Source)
				{
				case EStaleQuestTagSource::ActorBlueprintCDO:
					IconBrush = FAppStyle::Get().GetBrush("BlueprintEditor.FindInBlueprint");
					Tooltip = LOCTEXT("FocusTooltip_BP", "Open this Blueprint in the Blueprint editor.");
					break;
				case EStaleQuestTagSource::UnloadedLevelInstance:
					IconBrush = FAppStyle::Get().GetBrush("SystemWideCommands.OpenLevel");
					Tooltip = LOCTEXT("FocusTooltip_Unloaded", "Load this level and open it in the editor (replaces the current level — unsaved changes will prompt). The actor will be selected and framed once the level finishes loading.");
					break;
				default: break;
				}
			}
			return WithStripe(SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(2.f)
						.ToolTipText(Tooltip)
						.OnClicked(OnFocusClicked)
						[
							SNew(SImage)
								.Image(IconBrush)
								.ColorAndOpacity(FSlateColor::UseForeground())
								.DesiredSizeOverride(FVector2D(16.f, 16.f))
						]
				]);
		}
		if (ColumnName == Col_Source)
			return WithStripe(TextCell(Entry.IsValid() ? SourceLabel(Entry->Source) : FString()));
		if (ColumnName == Col_Level)
		{
			using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;
			if (!Entry.IsValid()) return WithStripe(TextCell(FString()));

			// BP CDO entries have no level — render an em-dash with a muted color and a "not applicable"
			// tooltip. The underlying value (per GetColumnText) stays empty so sort + filter naturally
			// exclude these from level-text matches.
			if (Entry->Source == EStaleQuestTagSource::ActorBlueprintCDO)
			{
				return WithStripe(SNew(SBox).VAlign(VAlign_Center).Padding(FMargin(4.f, 0.f))
					[
						SNew(STextBlock)
							.Text(FText::FromString(TEXT("—")))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.ToolTipText(LOCTEXT("LevelNA",
								"Not applicable — Blueprint defaults aren't level-bound. The entry's source is the BP asset itself."))
					]);
			}

			// Loaded / Unloaded: display the leaf name for readability; full umap path on hover for precision.
			// Sort + filter still operate on the full path so designers can type partial paths and get matches.
			const FString FullPath = Entry->PackagePath;
			const FString LeafName = FullPath.IsEmpty() ? FString() : FPackageName::GetShortName(FullPath);
			return WithStripe(SNew(SBox).VAlign(VAlign_Center).Padding(FMargin(4.f, 0.f))
				[
					SNew(STextBlock)
						.Text(FText::FromString(LeafName))
						.HighlightText(HighlightText)
						.ToolTipText(FText::FromString(FullPath))
				]);
		}
		if (ColumnName == Col_Actor)
			return WithStripe(TextCell(Entry->Actor.IsValid() ? Entry->Actor->GetActorNameOrLabel() : TEXT("<gc>")));
		if (ColumnName == Col_Comp)
			return WithStripe(TextCell(Entry->Component.IsValid() ? Entry->Component->GetClass()->GetName() : TEXT("<gc>")));
		if (ColumnName == Col_Field)
			return WithStripe(TextCell(Entry->FieldLabel));
		if (ColumnName == Col_Tag)
			return WithStripe(TextCell(Entry->StaleTag.ToString()));
		if (ColumnName == Col_Actions)
		{
			FText Tooltip = LOCTEXT("ClearTooltip_Loaded", "Remove this stale tag from the component. Marks the owning actor's level dirty — save the level to persist.");
			if (Entry.IsValid())
			{
				switch (Entry->Source)
				{
				case EStaleQuestTagSource::ActorBlueprintCDO:
					Tooltip = LOCTEXT("ClearTooltip_BP",
						"Remove this stale tag from the Blueprint's authored default. Marks the BP dirty — save the BP to persist. "
						"Note: existing actor instances may carry per-instance overrides; rescan after to confirm.");
					break;
				case EStaleQuestTagSource::UnloadedLevelInstance:
					Tooltip = LOCTEXT("ClearTooltip_Unloaded",
						"Remove this stale tag from the actor in the unloaded level. The level isn't open in the editor — use Save All Modified to persist.");
					break;
				default: break;
				}
			}
			return WithStripe(SNew(SBox).VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
					[
						SNew(SButton)
							.Text(LOCTEXT("Clear", "Clear"))
							.ToolTipText(Tooltip)
							.OnClicked(OnClearClicked)
					]
				]);
		}
		return SNullWidget::NullWidget;
	}

private:
	SStaleQuestTagsPanel::FEntryPtr Entry;
	TAttribute<FText> HighlightText;
	FOnClicked OnFocusClicked;
	FOnClicked OnClearClicked;

	FSlateColor GetStripeColor() const
	{
		return (IndexInList % 2 == 0)
			? FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.15f))
			: FSlateColor(FLinearColor::Transparent);
	}
};

void SStaleQuestTagsPanel::Construct(const FArguments& InArgs)
{
	if (GEditor) GEditor->RegisterForUndo(this);

	CurrentSortColumn = Col_Actor;

	ChildSlot
		.Padding(FMargin(4.f, 0.f, 4.f, 4.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
					.Text(LOCTEXT("ScanLoaded", "Scan Loaded"))
					.ToolTipText(LOCTEXT("ScanLoadedTooltip",
						"Re-scan loaded editor worlds for components with stale tag references. Fast — for "
						"designer iteration. Use Full Project Scan to also include Blueprint defaults and "
						"unloaded levels."))
					.OnClicked(this, &SStaleQuestTagsPanel::HandleScanLoadedClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
					.Text(LOCTEXT("FullScan", "Full Project Scan"))
					.ToolTipText(LOCTEXT("FullScanTooltip",
						"Scan loaded editor worlds, every actor-derived Blueprint's CDO, AND every unloaded level "
						"(including World Partition levels). Slower than Refresh because of sync-loading; intended "
						"as a pre-flight pass before tagged operations like a release tag or a tag namespace "
						"consolidation. Wrapped in a progress dialog."))
					.OnClicked(this, &SStaleQuestTagsPanel::HandleFullScanClicked)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
			[
				SNew(SSearchBox)
					.HintText(LOCTEXT("FilterHint", "Filter rows..."))
					.ToolTipText(LOCTEXT("FilterTooltip", "Filter rows by source, actor, component, field, or stale tag. Case-insensitive substring match; matching text is highlighted in visible rows."))
					.OnTextChanged(this, &SStaleQuestTagsPanel::HandleFilterTextChanged)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
					.IsEnabled(this, &SStaleQuestTagsPanel::IsClearSelectedEnabled)
					.Text(this, &SStaleQuestTagsPanel::GetClearSelectedLabel)
					.ToolTipText(LOCTEXT("ClearSelectedTooltip",
						"Clear every stale tag reference in the current selection. Multi-select via Ctrl+Click "
						"or Shift+Click. A confirmation dialog shows the per-source breakdown before mutation. "
						"The entire batch is wrapped in one transaction — Ctrl+Z restores all cleared tags. "
						"Disabled when nothing is selected."))
					.OnClicked(this, &SStaleQuestTagsPanel::HandleClearSelectedClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
					.IsEnabled(this, &SStaleQuestTagsPanel::IsSaveAllModifiedEnabled)
					.Text(this, &SStaleQuestTagsPanel::GetSaveAllModifiedLabel)
					.ToolTipText(LOCTEXT("SaveAllModifiedTooltip",
						"Save every asset modified by this panel's Clear actions — levels, Blueprints, and "
						"unloaded-level packages. Disabled when nothing is pending. Tracking is cleared per "
						"package as it's saved (here or via File → Save All)."))
					.OnClicked(this, &SStaleQuestTagsPanel::HandleSaveAllModifiedClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &SStaleQuestTagsPanel::GetStatusText)
			]
		]
		+ SVerticalBox::Slot().AutoHeight() [ SNew(SSeparator) ]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(ListView, SListView<FEntryPtr>)
					.Visibility(this, &SStaleQuestTagsPanel::GetListVisibility)
					.ListItemsSource(&VisibleEntries)
					.OnGenerateRow(this, &SStaleQuestTagsPanel::HandleGenerateRow)
					.SelectionMode(ESelectionMode::Multi)
				.HeaderRow(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(Col_Find).DefaultLabel(FText::GetEmpty()).FixedWidth(32.f)
					+ SHeaderRow::Column(Col_Source).DefaultLabel(LOCTEXT("ColSource", "Source")).FixedWidth(72.f)
						.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Source)
						.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
					+ SHeaderRow::Column(Col_Level).DefaultLabel(LOCTEXT("ColLevel", "Level")).FillWidth(0.15f)
						.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Level)
						.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
					+ SHeaderRow::Column(Col_Actor).DefaultLabel(LOCTEXT("ColActor", "Actor")).FillWidth(0.25f)
						.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Actor)
						.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
					+ SHeaderRow::Column(Col_Comp).DefaultLabel(LOCTEXT("ColComp", "Component")).FillWidth(0.20f)
						.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Comp)
						.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
					+ SHeaderRow::Column(Col_Field).DefaultLabel(LOCTEXT("ColField", "Field")).FillWidth(0.10f)
						.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Field)
						.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
					+ SHeaderRow::Column(Col_Tag).DefaultLabel(LOCTEXT("ColTag", "Stale Tag")).FillWidth(0.30f)
						.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Tag)
						.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
					+ SHeaderRow::Column(Col_Actions).DefaultLabel(FText::GetEmpty()).FixedWidth(80.f)
				)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
					.Visibility(this, &SStaleQuestTagsPanel::GetEmptyMessageVisibility)
					.Text(LOCTEXT("Empty", "No stale quest-tag references found. Use Refresh for loaded-level scope, or Full Project Scan to also include actor Blueprints and unloaded levels."))
			]
		]
	];

	Refresh();
}

SStaleQuestTagsPanel::~SStaleQuestTagsPanel()
{
	if (GEditor) GEditor->UnregisterForUndo(this);
}

void SStaleQuestTagsPanel::PostUndo(bool bSuccess)
{
	// Per-actor targeted rescan — replaces the prior Refresh(strippedScope) approach. For each actor we've
	// Cleared on this session, re-walk its components via ScanActorForStaleTags and replace its entries in
	// AllEntries with fresh scan results. Sub-millisecond per actor, vs 1–2 s for even the stripped Refresh.
	//
	// Correctness: the rescan emits entries with fresh weak ptrs to the *current* world's component instances,
	// sidestepping the post-undo weak-ptr-invalidation that would otherwise drop sibling rows. Whatever the
	// transaction restored is what the scan sees — including tags that came back via undo and tags that were
	// never modified at all (sibling entries on the same actor).
	UpdateFromAffectedActors();
}

void SStaleQuestTagsPanel::PostRedo(bool bSuccess)
{
	// Symmetric: same per-actor rescan path. A redo re-removes the tag from the actor; the rescan emits no
	// entry for it, AllEntries reflects the redo. Sibling entries that weren't part of the redone transaction
	// are still in the actor's container and re-emit normally.
	UpdateFromAffectedActors();
}

void SStaleQuestTagsPanel::UpdateFromAffectedActors()
{
	// Pass 1: drop any entries in AllEntries that point at actors in our tracked set. They'll be replaced
	// by fresh scan results below. Use the ptr identity, not the source/path, since a single actor could
	// in theory have multiple entries from different scan passes.
	AllEntries.RemoveAll([this](const FEntryPtr& E)
	{
		return E.IsValid() && E->Actor.IsValid() && ActorsTouchedByClear.Contains(E->Actor);
	});

	// Pass 2: for each tracked actor still alive, run a targeted scan and append fresh entries. Drop tracked
	// actors whose weak ptrs have gone stale (e.g. their level was unloaded and the actor was gc'd) — those
	// will need a manual Refresh / Full Project Scan to recover.
	TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry> Fresh;
	for (auto It = ActorsTouchedByClear.CreateIterator(); It; ++It)
	{
		AActor* Actor = It->Key.Get();
		if (!Actor)
		{
			It.RemoveCurrent();
			continue;
		}
		FSimpleQuestEditorUtilities::ScanActorForStaleTags(Actor, It->Value.Source, It->Value.PackagePath, Fresh);
	}
	for (FSimpleQuestEditorUtilities::FStaleQuestTagEntry& Raw : Fresh)
	{
		AllEntries.Add(MakeShared<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>(MoveTemp(Raw)));
	}

	RebuildVisibleList();
	if (ListView.IsValid()) ListView->RequestListRefresh();
}

TSharedRef<ITableRow> SStaleQuestTagsPanel::HandleGenerateRow(FEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SStaleQuestTagRow, OwnerTable)
		.Entry(Item)
		.HighlightText(this, &SStaleQuestTagsPanel::GetHighlightText)
		.OnFocusClicked(FOnClicked::CreateSP(this, &SStaleQuestTagsPanel::HandleFocusClicked, Item))
		.OnClearClicked(FOnClicked::CreateSP(this, &SStaleQuestTagsPanel::HandleClearClicked, Item));
}

EVisibility SStaleQuestTagsPanel::GetEmptyMessageVisibility() const { return VisibleEntries.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; }
EVisibility SStaleQuestTagsPanel::GetListVisibility() const        { return VisibleEntries.Num() == 0 ? EVisibility::Collapsed : EVisibility::Visible; }

FText SStaleQuestTagsPanel::GetStatusText() const
{
	if (AllEntries.Num() == 0) return LOCTEXT("StatusClean", "Clean — no stale references.");
	if (VisibleEntries.Num() == AllEntries.Num())
	{
		return FText::Format(LOCTEXT("StatusFmtAll", "{0} stale reference(s)"), AllEntries.Num());
	}
	return FText::Format(LOCTEXT("StatusFmtFiltered", "{0} of {1} stale reference(s) shown"),
		VisibleEntries.Num(), AllEntries.Num());
}

int32 SStaleQuestTagsPanel::GetPendingSaveCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<UPackage>& WeakPkg : ModifiedPackages)
	{
		if (UPackage* Pkg = WeakPkg.Get())
		{
			if (Pkg->IsDirty()) ++Count;
		}
	}
	return Count;
}

bool SStaleQuestTagsPanel::IsSaveAllModifiedEnabled() const
{
	return GetPendingSaveCount() > 0;
}

FText SStaleQuestTagsPanel::GetSaveAllModifiedLabel() const
{
	const int32 Count = GetPendingSaveCount();
	if (Count == 0) return LOCTEXT("SaveAllModifiedZero", "Save All Modified");
	return FText::Format(LOCTEXT("SaveAllModifiedFmt", "Save All Modified ({0})"), Count);
}

FReply SStaleQuestTagsPanel::HandleSaveAllModifiedClicked()
{
	// Collect packages that are still dirty (some may have been saved via File → Save All independently of this panel).
	TArray<UPackage*> ToSave;
	for (auto It = ModifiedPackages.CreateIterator(); It; ++It)
	{
		UPackage* Pkg = It->Get();
		if (!Pkg) { It.RemoveCurrent(); continue; }
		if (Pkg->IsDirty()) ToSave.Add(Pkg);
		else                It.RemoveCurrent();  // already saved out-of-band; drop from tracking
	}
	if (ToSave.Num() == 0)
	{
		ModifiedPackages.Empty();
		return FReply::Handled();
	}

	// FEditorFileUtils::PromptForCheckoutAndSave handles SCC integration + the standard save-confirmation flow.
	// bCheckDirty=false because we already filtered; bPromptToSave=false to skip the redundant confirmation
	// (the designer just clicked the explicit Save All Modified button, that IS the confirmation).
	const FEditorFileUtils::EPromptReturnCode Result = FEditorFileUtils::PromptForCheckoutAndSave(
		ToSave,
		/*bCheckDirty*/ false,
		/*bPromptToSave*/ false);

	UE_LOG(LogSimpleQuest, Log, TEXT("SStaleQuestTagsPanel: SaveAllModified saved %d package(s), result=%d"),
		ToSave.Num(), static_cast<int32>(Result));

	// Drop saved entries from the tracking set; leave any still-dirty (save was cancelled / failed) for retry.
	for (auto It = ModifiedPackages.CreateIterator(); It; ++It)
	{
		UPackage* Pkg = It->Get();
		if (!Pkg || !Pkg->IsDirty()) It.RemoveCurrent();
	}

	return FReply::Handled();
}

FReply SStaleQuestTagsPanel::HandleScanLoadedClicked()
{
	Refresh();
	return FReply::Handled();
}

FReply SStaleQuestTagsPanel::HandleFullScanClicked()
{
	// Two-step progress: starts at 0% with the message visible while the scan runs, advances to 100%
	// once Refresh returns. The actual scan work doesn't have plumbed-through per-step progress (would
	// require threading a progress callback through ScanActorBlueprintCDOs / ScanUnloadedLevels /
	// ScanWorldPartitionActors), but the secondary asset-loading bar that UE shows during sync-loads
	// gives the designer real-time feedback that work is happening — the outer bar just frames the operation.
	FScopedSlowTask SlowTask(1.f, LOCTEXT("FullScanProgress", "Scanning project for stale quest tags..."));
	SlowTask.MakeDialog(false);

	FSimpleQuestEditorUtilities::FStaleTagScanScope Scope;
	Scope.bLoadedLevels = true;
	Scope.bActorBlueprintCDOs = true;
	Scope.bUnloadedLevels = true;
	Scope.bComprehensiveWPScan = true;  // default, but explicit for clarity at the call site
	Refresh(Scope);

	SlowTask.EnterProgressFrame(1.f);  // bar fills + dialog dismisses on scope exit
	return FReply::Handled();
}

FReply SStaleQuestTagsPanel::HandleClearClicked(FEntryPtr Entry)
{
	if (!Entry.IsValid() || !Entry->Component.IsValid()) return FReply::Handled();

	int32 Removed = 0;
	UPackage* DirtiedPackage = nullptr;
	{
		// One scoped transaction wraps the mutation + dirty calls so undo restores the pre-Clear state.
		const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "ClearStaleTag", "Clear Stale Quest Tag"));
		DirtiedPackage = ClearOneEntry(Entry, Removed);
	}

	if (DirtiedPackage)
	{
		ModifiedPackages.Add(DirtiedPackage);
	}

	UE_LOG(LogSimpleQuest, Log, TEXT("SStaleQuestTagsPanel: cleared stale tag '%s' from %s on '%s' (%d removal(s), source=%s, dirtied=%s)"),
		*Entry->StaleTag.ToString(),
		*Entry->Component->GetClass()->GetName(),
		Entry->Actor.IsValid() ? *Entry->Actor->GetActorNameOrLabel() : TEXT("<gc>"),
		Removed,
		*SourceLabel(Entry->Source),
		DirtiedPackage ? *DirtiedPackage->GetName() : TEXT("(none)"));

	// Track the actor for targeted PostUndo / PostRedo rescan. BP CDO entries opt out — their actor is a CDO,
	// not a placed instance, and the per-actor rescan path is for level instances only.
	if (Entry->Source != FSimpleQuestEditorUtilities::EStaleQuestTagSource::ActorBlueprintCDO
		&& Entry->Actor.IsValid())
	{
		ActorsTouchedByClear.FindOrAdd(Entry->Actor) = { Entry->Source, Entry->PackagePath };
	}

	// Remove the cleared row locally rather than re-scanning. A re-scan with the panel's default (Tier 1) scope
	// would silently drop any BP CDO / unloaded-level rows from a prior Full Project Scan; we want the rest of
	// the displayed list to survive a Clear unchanged. The cleared entry's component no longer carries the tag,
	// so there's nothing for a re-scan to do anyway.
	AllEntries.RemoveAll([&Entry](const FEntryPtr& E) { return E == Entry; });
	RebuildVisibleList();
	if (ListView.IsValid()) ListView->RequestListRefresh();
	return FReply::Handled();
}

UPackage* SStaleQuestTagsPanel::ClearOneEntry(FEntryPtr Entry, int32& OutRemoved)
{
	OutRemoved = 0;
	if (!Entry.IsValid() || !Entry->Component.IsValid()) return nullptr;

	using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;

	OutRemoved = Entry->Component->RemoveTags({ Entry->StaleTag });

	UPackage* DirtiedPackage = nullptr;
	switch (Entry->Source)
	{
		case EStaleQuestTagSource::LoadedLevelInstance:
		{
			if (AActor* Actor = Entry->Actor.Get())
			{
				DirtiedPackage = Actor->GetPackage();
				Actor->MarkPackageDirty();
			}
			break;
		}

		case EStaleQuestTagSource::ActorBlueprintCDO:
		{
			// Walk the component template's outer chain to find the BP. SCS templates' outer is a USCS_Node;
			// ICH templates' outer is a UInheritableComponentHandler; native CDO subobjects' outer is the
			// CDO (a UBlueprintGeneratedClass instance). All three paths bottom out at a UBlueprintGenerated-
			// Class whose ClassGeneratedBy is the UBlueprint we want.
			UBlueprint* BP = nullptr;
			for (UObject* Outer = Entry->Component->GetOuter(); Outer; Outer = Outer->GetOuter())
			{
				if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Outer))
				{
					BP = Cast<UBlueprint>(BPGC->ClassGeneratedBy);
					break;
				}
			}
			// Fallback: resolve via PackagePath if outer-chain walk didn't land on a BPGC (rare).
			if (!BP && !Entry->PackagePath.IsEmpty())
			{
				const FString BPObjectPath = Entry->PackagePath + TEXT(".") + FPackageName::GetShortName(Entry->PackagePath);
				BP = LoadObject<UBlueprint>(nullptr, *BPObjectPath);
			}
			if (BP)
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);  // flags for recompile + transactional Modify
				DirtiedPackage = BP->GetPackage();
			}
			break;
		}

		case EStaleQuestTagSource::UnloadedLevelInstance:
		{
			// Both umap actors and WP external-actor instances dirty their own package via MarkPackageDirty —
			// for non-WP the package is the umap; for WP it's the per-actor external .uasset. Either way
			// FEditorFileUtils::SaveDirtyPackages handles the save when the designer clicks Save All Modified.
			if (AActor* Actor = Entry->Actor.Get())
			{
				DirtiedPackage = Actor->GetPackage();
				Actor->MarkPackageDirty();
			}
			break;
		}
	}

	return DirtiedPackage;
}

bool SStaleQuestTagsPanel::IsClearSelectedEnabled() const
{
	return ListView.IsValid() && ListView->GetNumItemsSelected() > 0;
}

FText SStaleQuestTagsPanel::GetClearSelectedLabel() const
{
	const int32 Count = ListView.IsValid() ? ListView->GetNumItemsSelected() : 0;
	if (Count == 0) return LOCTEXT("ClearSelectedZero", "Clear Selected");
	return FText::Format(LOCTEXT("ClearSelectedFmt", "Clear Selected ({0})"), Count);
}

FReply SStaleQuestTagsPanel::HandleClearSelectedClicked()
{
	if (!ListView.IsValid()) return FReply::Handled();

	TArray<FEntryPtr> Selected = ListView->GetSelectedItems();
	if (Selected.Num() == 0) return FReply::Handled();

	using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;

	// Per-source breakdown for the confirmation dialog. Helps the designer verify the selection matches
	// intent before a transactional bulk mutation that could span loaded levels, BPs, and unloaded levels.
	int32 NumLoaded = 0, NumBPCDO = 0, NumUnloaded = 0;
	TSet<FString> UniquePackages;
	for (const FEntryPtr& E : Selected)
	{
		if (!E.IsValid()) continue;
		switch (E->Source)
		{
			case EStaleQuestTagSource::LoadedLevelInstance:    ++NumLoaded;   break;
			case EStaleQuestTagSource::ActorBlueprintCDO:      ++NumBPCDO;    break;
			case EStaleQuestTagSource::UnloadedLevelInstance:  ++NumUnloaded; break;
			default: break;
		}
		if (!E->PackagePath.IsEmpty()) UniquePackages.Add(E->PackagePath);
	}

	// Build the breakdown text. Skip zero-count categories so the dialog stays readable for narrow selections.
	FString BreakdownLines;
	if (NumLoaded   > 0) BreakdownLines += FString::Printf(TEXT("\n  • %d loaded level entr%s"), NumLoaded,   NumLoaded   == 1 ? TEXT("y") : TEXT("ies"));
	if (NumBPCDO    > 0) BreakdownLines += FString::Printf(TEXT("\n  • %d Blueprint default%s"), NumBPCDO,    NumBPCDO    == 1 ? TEXT("")  : TEXT("s"));
	if (NumUnloaded > 0) BreakdownLines += FString::Printf(TEXT("\n  • %d unloaded level entr%s"), NumUnloaded, NumUnloaded == 1 ? TEXT("y") : TEXT("ies"));

	const FText ConfirmText = FText::Format(
		LOCTEXT("ClearSelectedConfirmFmt",
			"Clear {0} stale tag reference(s) across {1} unique asset(s)?{2}\n\n"
			"This is wrapped in a single transaction — Ctrl+Z restores the entire batch."),
		FText::AsNumber(Selected.Num()),
		FText::AsNumber(UniquePackages.Num()),
		FText::FromString(BreakdownLines));

	if (FMessageDialog::Open(EAppMsgType::OkCancel, ConfirmText) != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	int32 TotalRemoved = 0;
	int32 SkippedStale = 0;
	TSet<UPackage*> DirtiedThisBatch;
	{
		// Single transaction wraps the entire batch — Ctrl+Z restores all cleared tags as one unit.
		const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "ClearSelectedStaleTags", "Clear Selected Stale Quest Tags"));
		for (const FEntryPtr& E : Selected)
		{
			int32 Removed = 0;
			UPackage* Dirtied = ClearOneEntry(E, Removed);
			if (!E.IsValid() || !E->Component.IsValid()) { ++SkippedStale; continue; }
			TotalRemoved += Removed;
			if (Dirtied) DirtiedThisBatch.Add(Dirtied);
		}
	}

	for (UPackage* Pkg : DirtiedThisBatch)
	{
		ModifiedPackages.Add(Pkg);
	}

	UE_LOG(LogSimpleQuest, Log,
		TEXT("SStaleQuestTagsPanel: bulk-cleared %d entr%s (%d tag removal(s), %d unique package(s) dirtied, %d skipped as gc'd; loaded=%d bpcdo=%d unloaded=%d)"),
		Selected.Num() - SkippedStale,
		(Selected.Num() - SkippedStale) == 1 ? TEXT("y") : TEXT("ies"),
		TotalRemoved,
		DirtiedThisBatch.Num(),
		SkippedStale,
		NumLoaded, NumBPCDO, NumUnloaded);

	// Track every actor in the selection for targeted PostUndo / PostRedo rescan. Same opt-out for BP CDOs.
	for (const FEntryPtr& E : Selected)
	{
		if (!E.IsValid()) continue;
		if (E->Source == FSimpleQuestEditorUtilities::EStaleQuestTagSource::ActorBlueprintCDO) continue;
		if (!E->Actor.IsValid()) continue;
		ActorsTouchedByClear.FindOrAdd(E->Actor) = { E->Source, E->PackagePath };
	}

	// Remove cleared rows locally — same rationale as single-row Clear (avoids dropping out-of-scope rows
	// that a default-scope re-scan would silently lose).
	AllEntries.RemoveAll([&Selected](const FEntryPtr& E) { return Selected.Contains(E); });
	ListView->ClearSelection();
	RebuildVisibleList();
	ListView->RequestListRefresh();
	return FReply::Handled();
}



FReply SStaleQuestTagsPanel::HandleFocusClicked(FEntryPtr Entry)
{
	if (!Entry.IsValid() || !GEditor) return FReply::Handled();

	using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;

	switch (Entry->Source)
	{
		case EStaleQuestTagSource::LoadedLevelInstance:
		{
			// Existing behavior — select + frame.
			AActor* Actor = Entry->Actor.Get();
			if (!Actor) return FReply::Handled();
			GEditor->SelectNone(true, true, false);
			GEditor->SelectActor(Actor, true, true);
			GEditor->MoveViewportCamerasToActor(*Actor, false);
			return FReply::Handled();
		}

		case EStaleQuestTagSource::ActorBlueprintCDO:
		{
			// Sync-load the BP and route to the asset editor subsystem. PackagePath holds the BP's long
			// package name; LoadObject<UBlueprint> resolves directly.
			if (Entry->PackagePath.IsEmpty()) return FReply::Handled();
			const FString BPObjectPath = Entry->PackagePath + TEXT(".") + FPackageName::GetShortName(Entry->PackagePath);
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPObjectPath);
			if (!BP) return FReply::Handled();
			if (UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AES->OpenEditorForAsset(BP);
			}
			return FReply::Handled();
		}

		case EStaleQuestTagSource::UnloadedLevelInstance:
		{
			if (Entry->PackagePath.IsEmpty()) return FReply::Handled();

			// Capture the actor's FName before any load — the weak ptr we have is still valid at this point
			// (the entry was emitted by a sync-load that kept the world resident), but LoadMap may invalidate
			// it. We'll use the FName to re-resolve the actor in the newly-loaded world post-load. FNames are
			// unique within a UWorld so this is a safe lookup key for the typical authored-actor case.
			const FName ActorName = Entry->Actor.IsValid() ? Entry->Actor->GetFName() : NAME_None;
			const FString PackagePath = Entry->PackagePath;  // copy — Entry might morph + go away after Refresh

			// Idempotency: if the level is ALREADY an active editor world, skip the LoadMap call (which would
			// reload the level from scratch) and fall straight through to the select+frame path. This handles
			// the "designer clicks the LevelOpen icon twice before the row's icon morphs" case.
			bool bAlreadyLoaded = false;
			if (GEditor)
			{
				for (const FWorldContext& Context : GEditor->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Editor) continue;
					UWorld* World = Context.World();
					if (!World) continue;
					if (UPackage* Pkg = World->GetOutermost())
					{
						if (Pkg->GetName() == PackagePath) { bAlreadyLoaded = true; break; }
					}
				}
			}

			if (!bAlreadyLoaded)
			{
				FString MapFilename;
				if (!FPackageName::TryConvertLongPackageNameToFilename(PackagePath, MapFilename, FPackageName::GetMapPackageExtension()))
				{
					UE_LOG(LogSimpleQuest, Warning, TEXT("HandleFocusClicked: could not resolve filename for unloaded level package '%s'"),
						*PackagePath);
					return FReply::Handled();
				}
				FEditorFileUtils::LoadMap(MapFilename, /*LoadAsTemplate*/ false, /*bShowProgress*/ true);

				// Auto-refresh post-load so any preserved entries from this level morph from Unloaded to
				// Loaded source — their icons, tooltips, and Source-column labels update reactively, and
				// the in-scope replacement also kicks in (the morphed Loaded entries get replaced by fresh
				// scan results with valid Actor weak ptrs).
				Refresh(LastScope);
			}

			// Re-locate the actor in the now-loaded world by FName and select + frame. After Refresh the
			// original entry's TSharedPtr may have been replaced by a fresh one with a valid Actor weak ptr,
			// but rather than chase that we just walk the editor worlds for the matching FName — uniform
			// path whether we just loaded or the level was already loaded.
			if (ActorName != NAME_None && GEditor)
			{
				for (const FWorldContext& Context : GEditor->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Editor) continue;
					UWorld* World = Context.World();
					if (!World) continue;
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						if (*It && (*It)->GetFName() == ActorName)
						{
							GEditor->SelectNone(/*bNoteSelectionChange*/ true, /*bDeselectBSPSurfs*/ true, /*WarnAboutManyActors*/ false);
							GEditor->SelectActor(*It, /*bInSelected*/ true, /*bNotify*/ true);
							GEditor->MoveViewportCamerasToActor(**It, /*bActiveViewportOnly*/ false);
							return FReply::Handled();
						}
					}
				}
				UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleFocusClicked: actor '%s' not found in newly-loaded world '%s' — selection skipped"),
					*ActorName.ToString(), *PackagePath);
			}
			return FReply::Handled();
		}

		default:
			return FReply::Handled();
	}
}

void SStaleQuestTagsPanel::HandleFilterTextChanged(const FText& NewText)
{
	FilterText = NewText;
	RebuildVisibleList();
	if (ListView.IsValid()) ListView->RequestListRefresh();
}

EColumnSortMode::Type SStaleQuestTagsPanel::GetColumnSortMode(FName ColumnId) const
{
	return (ColumnId == CurrentSortColumn) ? CurrentSortMode : EColumnSortMode::None;
}

void SStaleQuestTagsPanel::HandleSortColumn(EColumnSortPriority::Type /*SortPriority*/, const FName& ColumnId, EColumnSortMode::Type NewMode)
{
	CurrentSortColumn = ColumnId;
	CurrentSortMode = NewMode;
	RebuildVisibleList();
	if (ListView.IsValid()) ListView->RequestListRefresh();
}

void SStaleQuestTagsPanel::Refresh(FSimpleQuestEditorUtilities::FStaleTagScanScope Scope)
{
	LastScope = Scope;  // remembered for undo / redo so the post-restore view matches the pre-restore view

	using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;

	// Build a snapshot of currently-active editor world packages. Used to refine the in-scope check for
	// Loaded entries: a Loaded entry is only re-verifiable if its level is currently loaded as an editor
	// world. If the designer has switched levels since the entry was captured, Tier 1 can't reach it —
	// we should preserve the prior entry rather than drop it as if it were verified clean.
	TSet<FName> EditorWorldPackages;
	if (GEditor)
	{
		for (const FWorldContext& Context : GEditor->GetWorldContexts())
		{
			if (Context.WorldType != EWorldType::Editor) continue;
			if (UWorld* ActiveWorld = Context.World())
			{
				if (UPackage* Pkg = ActiveWorld->GetOutermost())
				{
					EditorWorldPackages.Add(Pkg->GetFName());
				}
			}
		}
	}

	// Step 0: morph entries' Source based on current package-load state. Source is captured at scan time
	// but the actual loaded/unloaded status of the underlying package can change afterward (designer opens
	// or closes a level). A Loaded entry whose level got unloaded should now read as Unloaded — its icon,
	// tooltip, and click action all need to track to "open this level" rather than "select in viewport"
	// to be useful. Conversely, an Unloaded entry whose level got opened should now read as Loaded so the
	// Find icon does the right thing. BP CDO entries don't have a load axis in this sense — their identity
	// is the Blueprint asset, not a per-package world state.
	//
	// Doing the morph BEFORE the in-scope check has a useful side effect: it also fixes the duplicate-row
	// issue noted in the prior change. If a previously-Unloaded entry's level is now active, it morphs to
	// Loaded → falls into Tier 1's scope → gets removed and replaced by the fresh scan's matching entry,
	// rather than persisting as a stale Unloaded ghost alongside the new Loaded row.
	for (int32 i = 0; i < AllEntries.Num(); ++i)
	{
		FEntryPtr& E = AllEntries[i];
		if (!E.IsValid()) continue;
		if (E->Source == EStaleQuestTagSource::ActorBlueprintCDO) continue;

		const bool bIsCurrentlyLoaded = EditorWorldPackages.Contains(FName(*E->PackagePath));
		EStaleQuestTagSource NewSource = E->Source;
		if (bIsCurrentlyLoaded && E->Source == EStaleQuestTagSource::UnloadedLevelInstance)
		{
			NewSource = EStaleQuestTagSource::LoadedLevelInstance;
		}
		else if (!bIsCurrentlyLoaded && E->Source == EStaleQuestTagSource::LoadedLevelInstance)
		{
			NewSource = EStaleQuestTagSource::UnloadedLevelInstance;
		}

		if (NewSource != E->Source)
		{
			// Replace with a fresh TSharedPtr carrying the new Source. The fresh identity invalidates
			// SListView's per-item widget cache for this row, forcing regeneration on the next refresh
			// — that picks up the new icon / tooltip / Source label without per-paint lambda overhead.
			FSimpleQuestEditorUtilities::FStaleQuestTagEntry Mutated = *E;
			Mutated.Source = NewSource;
			E = MakeShared<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>(MoveTemp(Mutated));
		}
	}

	// Each scan can only authoritatively answer "is this still stale?" for entries it actually verified.
	// In-scope entries get replaced with fresh scan results; out-of-scope entries persist as-is. The
	// granularity is per-entry, not per-source-type — for Loaded entries specifically we ALSO check that
	// the entry's level is currently an active editor world, because Tier 1 can only re-verify levels the
	// designer has loaded right now. Concrete case: scan Level A → switch to Level B → Refresh. The Tier 1
	// scan walks Level B, but Level A's entries shouldn't disappear (Tier 1 didn't verify them — Level A
	// isn't loaded anymore — and Step 0 morphed them to Unloaded source so they're correctly out of scope).
	// BP CDO + Unloaded sources don't have the active-world complication: their scans walk the AR project-
	// wide, so any prior entry of those types is reachable when the source bit is set.
	auto IsEntryInScope = [&Scope, &EditorWorldPackages](const FEntryPtr& E) -> bool
	{
		if (!E.IsValid()) return false;
		switch (E->Source)
		{
			case EStaleQuestTagSource::LoadedLevelInstance:
				return Scope.bLoadedLevels && EditorWorldPackages.Contains(FName(*E->PackagePath));
			case EStaleQuestTagSource::ActorBlueprintCDO:
				return Scope.bActorBlueprintCDOs;
			case EStaleQuestTagSource::UnloadedLevelInstance:
				return Scope.bUnloadedLevels;
			default:
				return true;
		}
	};

	// Step 1: drop entries this scope DID verify — they'll be replaced with fresh scan results below.
	AllEntries.RemoveAll([&IsEntryInScope](const FEntryPtr& E)
	{
		return IsEntryInScope(E);
	});

	// Step 2: append the new scan's results. These are necessarily all from in-scope sources (the scan only
	// emits what its scope produces), so there's no in-scope/out-of-scope dispatch needed at the add stage.
	for (const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& Raw : FSimpleQuestEditorUtilities::CollectStaleQuestTagEntries(Scope))
	{
		AllEntries.Add(MakeShared<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>(Raw));
	}

	RebuildVisibleList();
	if (ListView.IsValid()) ListView->RequestListRefresh();
}

namespace
{
	FString GetColumnText(const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& E, FName ColumnId)
	{
		if (ColumnId == Col_Source) return SourceLabel(E.Source);
		if (ColumnId == Col_Level)
		{
			// BP CDO entries deliberately return empty — they have no level, and the empty value makes
			// them invisible to level-text filters and clusters them at one end of any level sort.
			if (E.Source == FSimpleQuestEditorUtilities::EStaleQuestTagSource::ActorBlueprintCDO) return FString();
			return E.PackagePath;
		}
		if (ColumnId == Col_Actor)  return E.Actor.IsValid() ? E.Actor->GetActorNameOrLabel() : FString();
		if (ColumnId == Col_Comp)   return E.Component.IsValid() ? E.Component->GetClass()->GetName() : FString();
		if (ColumnId == Col_Field)  return E.FieldLabel;
		if (ColumnId == Col_Tag)    return E.StaleTag.ToString();
		return FString();
	}

	bool EntryMatchesFilter(const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& E, const FString& Filter)
	{
		// All sortable columns contribute to filter matching. Actions column is UI-only.
		static const FName ScanColumns[] = { Col_Source, Col_Level, Col_Actor, Col_Comp, Col_Field, Col_Tag };
		for (const FName& Col : ScanColumns)
		{
			if (GetColumnText(E, Col).Contains(Filter, ESearchCase::IgnoreCase)) return true;
		}
		return false;
	}
}

void SStaleQuestTagsPanel::RebuildVisibleList()
{
	VisibleEntries.Reset();
	VisibleEntries.Reserve(AllEntries.Num());

	const FString FilterStr = FilterText.ToString();
	const bool bHasFilter = !FilterStr.IsEmpty();

	for (const FEntryPtr& E : AllEntries)
	{
		if (!E.IsValid()) continue;
		if (bHasFilter && !EntryMatchesFilter(*E, FilterStr)) continue;
		VisibleEntries.Add(E);
	}

	// StableSort so equal-keyed rows preserve their pre-sort order across re-sorts (no shuffling on each
	// click). Explicit None-mode handling — UE's SHeaderRow cycles Ascending → Descending → None on
	// repeat clicks; without this branch, None falls through the ternary and behaves like Descending,
	// which makes "click until you back to unsorted" silently flip the order instead of disabling sort.
	VisibleEntries.StableSort([this](const FEntryPtr& A, const FEntryPtr& B)
	{
		if (!A.IsValid() || !B.IsValid()) return false;
		if (CurrentSortMode == EColumnSortMode::None) return false;  // preserve insertion order
		const int32 Cmp = GetColumnText(*A, CurrentSortColumn).Compare(GetColumnText(*B, CurrentSortColumn), ESearchCase::IgnoreCase);
		if (CurrentSortMode == EColumnSortMode::Ascending) return Cmp < 0;
		return Cmp > 0;  // Descending
	});
}

#undef LOCTEXT_NAMESPACE