// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SStaleQuestTagsPanel.h"

#include "Components/QuestComponentBase.h"
#include "SimpleQuestLog.h"
#include "ScopedTransaction.h"
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
	const FName Col_Actor = "Actor";
	const FName Col_Comp = "Component";
	const FName Col_Field = "Field";
	const FName Col_Tag = "StaleTag";
	const FName Col_Actions = "Actions";	
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
		// Zebra stripe per-cell. Each cell's SBorder picks its color from IndexInList via a TAttribute, so sorts
		// re-evaluate correctly. Because the borders use a solid white-brush image with shared color across all
		// cells of a row, they read visually as one continuous stripe.
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

		if (ColumnName == Col_Find)
		{
			return WithStripe(SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(2.f)
						.ToolTipText(LOCTEXT("FocusTooltip", "Select this actor in its level and frame the viewport on it."))
						.OnClicked(OnFocusClicked)
						[
							SNew(SImage)
								.Image(FAppStyle::Get().GetBrush("Icons.Search"))
								.ColorAndOpacity(FSlateColor::UseForeground())
						]
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
			return WithStripe(SNew(SBox).VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
					[
						SNew(SButton)
							.Text(LOCTEXT("Clear", "Clear"))
							.ToolTipText(LOCTEXT("ClearTooltip", "Remove this stale tag from the component. Marks the owning actor dirty."))
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
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.ToolTipText(LOCTEXT("RefreshTooltip", "Re-scan loaded editor worlds for components with stale tag references."))
					.OnClicked(this, &SStaleQuestTagsPanel::HandleRefreshClicked)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
			[
				SNew(SSearchBox)
					.HintText(LOCTEXT("FilterHint", "Filter rows..."))
					.ToolTipText(LOCTEXT("FilterTooltip", "Filter rows by actor, component, field, or stale tag. Case-insensitive substring match; matching text is highlighted in visible rows."))
					.OnTextChanged(this, &SStaleQuestTagsPanel::HandleFilterTextChanged)
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
					.SelectionMode(ESelectionMode::None)
					.HeaderRow(
						SNew(SHeaderRow)
						+ SHeaderRow::Column(Col_Find).DefaultLabel(FText::GetEmpty()).FixedWidth(32.f)
						+ SHeaderRow::Column(Col_Actor).DefaultLabel(LOCTEXT("ColActor", "Actor")).FillWidth(0.25f)
							.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Actor)
							.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
						+ SHeaderRow::Column(Col_Comp).DefaultLabel(LOCTEXT("ColComp", "Component")).FillWidth(0.25f)
							.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Comp)
							.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
						+ SHeaderRow::Column(Col_Field).DefaultLabel(LOCTEXT("ColField", "Field")).FillWidth(0.15f)
							.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Field)
							.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
						+ SHeaderRow::Column(Col_Tag).DefaultLabel(LOCTEXT("ColTag", "Stale Tag")).FillWidth(0.35f)
							.SortMode(this, &SStaleQuestTagsPanel::GetColumnSortMode, Col_Tag)
							.OnSort(this, &SStaleQuestTagsPanel::HandleSortColumn)
						+ SHeaderRow::Column(Col_Actions).DefaultLabel(FText::GetEmpty()).FixedWidth(80.f)
					)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
					.Visibility(this, &SStaleQuestTagsPanel::GetEmptyMessageVisibility)
					.Text(LOCTEXT("Empty", "No stale quest-tag references in loaded levels."))
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
	// A Clear action just got reversed — the stale tag is back on the component. Re-scan so the row
	// reappears. Equally handles the edge case where an unrelated undo ran and nothing changed for us
	// (Refresh is idempotent).
	Refresh();
}

void SStaleQuestTagsPanel::PostRedo(bool bSuccess)
{
	// Redo re-applies the Clear — row vanishes again. Same refresh path.
	PostUndo(bSuccess);
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

FReply SStaleQuestTagsPanel::HandleRefreshClicked()
{
	Refresh();
	return FReply::Handled();
}

FReply SStaleQuestTagsPanel::HandleClearClicked(FEntryPtr Entry)
{
	if (!Entry.IsValid() || !Entry->Component.IsValid()) return FReply::Handled();

	int32 Removed = 0;
	{
		// Scoped transaction so Ctrl+Z restores the cleared tag. Component->Modify() inside RemoveTags captures
		// the pre-change container state for the undo buffer.
		const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "ClearStaleTag", "Clear Stale Quest Tag"));
		Removed = Entry->Component->RemoveTags({ Entry->StaleTag });
	}

	UE_LOG(LogSimpleQuest, Log, TEXT("SStaleQuestTagsPanel: cleared stale tag '%s' from %s on '%s' (%d removal(s))"),
		*Entry->StaleTag.ToString(),
		*Entry->Component->GetClass()->GetName(),
		Entry->Actor.IsValid() ? *Entry->Actor->GetActorNameOrLabel() : TEXT("<gc>"),
		Removed);

	Refresh();
	return FReply::Handled();
}

FReply SStaleQuestTagsPanel::HandleFocusClicked(FEntryPtr Entry)
{
	if (!Entry.IsValid() || !Entry->Actor.IsValid() || !GEditor) return FReply::Handled();

	AActor* Actor = Entry->Actor.Get();
	if (!Actor) return FReply::Handled();

	// Mirror the editor's "F" framing — deselect, select, frame viewport on target.
	GEditor->SelectNone(/*bNoteSelectionChange*/ true, /*bDeselectBSPSurfs*/ true, /*WarnAboutManyActors*/ false);
	GEditor->SelectActor(Actor, /*bInSelected*/ true, /*bNotify*/ true);
	GEditor->MoveViewportCamerasToActor(*Actor, /*bActiveViewportOnly*/ false);
	return FReply::Handled();
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

void SStaleQuestTagsPanel::Refresh()
{
	AllEntries.Reset();
	for (const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& Raw : FSimpleQuestEditorUtilities::CollectStaleQuestTagEntries())
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
		if (ColumnId == Col_Actor) return E.Actor.IsValid() ? E.Actor->GetActorNameOrLabel() : FString();
		if (ColumnId == Col_Comp)  return E.Component.IsValid() ? E.Component->GetClass()->GetName() : FString();
		if (ColumnId == Col_Field) return E.FieldLabel;
		if (ColumnId == Col_Tag)   return E.StaleTag.ToString();
		return FString();
	}

	bool EntryMatchesFilter(const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& E, const FString& Filter)
	{
		// All sortable columns contribute to filter matching. Actions column is UI-only.
		static const FName ScanColumns[] = { Col_Actor, Col_Comp, Col_Field, Col_Tag };
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

	VisibleEntries.Sort([this](const FEntryPtr& A, const FEntryPtr& B)
	{
		if (!A.IsValid() || !B.IsValid()) return false;
		const int32 Cmp = GetColumnText(*A, CurrentSortColumn).Compare(GetColumnText(*B, CurrentSortColumn), ESearchCase::IgnoreCase);
		return (CurrentSortMode == EColumnSortMode::Ascending) ? (Cmp < 0) : (Cmp > 0);
	});
}

#undef LOCTEXT_NAMESPACE