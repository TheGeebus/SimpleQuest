// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/SQuestMappingDiscriminatorList.h"
#include "ScopedTransaction.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Nodes/QuestlineNodeBase.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SQuestMappingDiscriminatorList"

namespace
{
	const FName ColumnId_Value(TEXT("Value"));
	const FName ColumnId_Class(TEXT("Class"));
	const FName ColumnId_Remove(TEXT("Remove"));

	// Find the DiscriminatorClasses entry that carries a value normalizing to RowValue (any of its Values). The panel displays
	// raw values but matches the way the guard does (NormalizeDiscriminatorValue = trim+lower), so "Objective"/"objective" are
	// one entry, never two the guard would reject for collision. Returns the entry index (INDEX_NONE if none).
	int32 FindEntryIndexForValue(const UQuestImportMapping& Mapping, const FString& RowValue)
	{
		const FString Norm = NormalizeDiscriminatorValue(RowValue);
		for (int32 i = 0; i < Mapping.DiscriminatorClasses.Num(); ++i)
		{
			for (const FString& V : Mapping.DiscriminatorClasses[i].Values)
			{
				if (NormalizeDiscriminatorValue(V) == Norm) { return i; }
			}
		}
		return INDEX_NONE;
	}
}

// ── Row ────────────────────────────────────────────────────────────────────────────────────────────────────────────

void SQuestMappingDiscriminatorRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable,
	TSharedRef<FQuestDiscriminatorRowItem> InItem, TSharedPtr<SQuestMappingDiscriminatorList> InList)
{
	Item = InItem;
	List = InList;
	SMultiColumnTableRow<FQuestDiscriminatorRowItemPtr>::Construct(FSuperRowType::FArguments().Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row")), InOwnerTable);
}

TSharedRef<SWidget> SQuestMappingDiscriminatorRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	const TSharedPtr<FQuestDiscriminatorRowItem> I = Item.Pin();
	if (!I.IsValid()) return SNullWidget::NullWidget;

	if (ColumnName == ColumnId_Value)
	{
		// Stale values (stored but not in the current sample) render dimmed with a note, so a partial sample reads honestly.
		const FText Label = FText::FromString(I->Value);
		const FText Note = I->bInSample ? FText::GetEmpty() : LOCTEXT("NotInSample", " (not in sample)");
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.0f, 1.0f)
			[
				SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
					.ColorAndOpacity(I->bInSample ? FSlateColor::UseForeground() : FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 1.0f)
			[
				SNew(STextBlock).Text(Note).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.ItalicFont")))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.6f, 0.3f)))
					.ToolTipText(LOCTEXT("StaleTip", "This value is stored on the recipe but is not present in the current sample source. Remove it, or point at a sample that contains it."))
			];
	}

	if (ColumnName == ColumnId_Class)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(3.0f, 1.0f)
			[
				SNew(SClassPropertyEntryBox)
					.MetaClass(UQuestlineNodeBase::StaticClass())
					.AllowNone(true)
					.AllowAbstract(false)
					.IsBlueprintBaseOnly(false)
					.SelectedClass(this, &SQuestMappingDiscriminatorRow::GetSelectedClass)
					.OnSetClass(this, &SQuestMappingDiscriminatorRow::OnSetClass)
			];
	}

	if (ColumnName == ColumnId_Remove)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 1.0f)
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
					.Visibility(this, &SQuestMappingDiscriminatorRow::GetRemoveVisibility)
					.OnClicked(this, &SQuestMappingDiscriminatorRow::OnRemoveClicked)
					.ToolTipText(LOCTEXT("RemoveStale", "Remove this stored mapping (its value isn't in the current sample)."))
					[
						SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))
					]
			];
	}

	return SNullWidget::NullWidget;
}

const UClass* SQuestMappingDiscriminatorRow::GetSelectedClass() const
{
	const TSharedPtr<FQuestDiscriminatorRowItem> I = Item.Pin();
	const TSharedPtr<SQuestMappingDiscriminatorList> L = List.Pin();
	if (!I.IsValid() || !L.IsValid()) return nullptr;
	const UQuestImportMapping* M = L->GetConfig().Mapping.Get();
	if (!M) return nullptr;
	const int32 Idx = FindEntryIndexForValue(*M, I->Value);
	if (Idx == INDEX_NONE) return nullptr;
	return M->DiscriminatorClasses[Idx].NodeClass.LoadSynchronous();
}

void SQuestMappingDiscriminatorRow::OnSetClass(const UClass* NewClass)
{
	const TSharedPtr<FQuestDiscriminatorRowItem> I = Item.Pin();
	const TSharedPtr<SQuestMappingDiscriminatorList> L = List.Pin();
	if (!I.IsValid() || !L.IsValid()) return;
	UQuestImportMapping* Mapping = L->GetConfig().Mapping.Get();
	if (!Mapping) return;

	const FScopedTransaction Transaction(LOCTEXT("SetDiscriminatorClass", "Set Discriminator Value Class"));
	Mapping->Modify();
	// Match the entry any of whose values normalizes to this row's value (update in place); else create a new entry keyed by
	// the normalized value, so the panel and the import guard agree from the first write — never store a raw-cased duplicate.
	const int32 Idx = FindEntryIndexForValue(*Mapping, I->Value);
	if (NewClass)
	{
		if (Idx != INDEX_NONE)
		{
			// Value already belongs to an entry — repoint that entry's class (all its values move together, by design).
			Mapping->DiscriminatorClasses[Idx].NodeClass = TSoftClassPtr<UQuestlineNodeBase>(const_cast<UClass*>(NewClass));
		}
		else
		{
			// New value -> new entry keyed by the normalized value; PrimaryValue seeds to it (single-value entry).
			FQuestDiscriminatorClass NewEntry;
			NewEntry.NodeClass = TSoftClassPtr<UQuestlineNodeBase>(const_cast<UClass*>(NewClass));
			NewEntry.Values.Add(NormalizeDiscriminatorValue(I->Value));
			NewEntry.PrimaryValue = NewEntry.Values[0];
			Mapping->DiscriminatorClasses.Add(MoveTemp(NewEntry));
		}
	}
	else if (Idx != INDEX_NONE)
	{
		// Clearing to None: drop this value from its entry; remove the entry if it becomes empty.
		FQuestDiscriminatorClass& Entry = Mapping->DiscriminatorClasses[Idx];
		Entry.Values.RemoveAll([&](const FString& V){ return NormalizeDiscriminatorValue(V) == NormalizeDiscriminatorValue(I->Value); });
		if (Entry.PrimaryValue.IsEmpty() || NormalizeDiscriminatorValue(Entry.PrimaryValue) == NormalizeDiscriminatorValue(I->Value))
		{
			Entry.PrimaryValue = Entry.Values.Num() > 0 ? Entry.Values[0] : FString();
		}
		if (Entry.Values.Num() == 0) { Mapping->DiscriminatorClasses.RemoveAt(Idx); }
	}
	Mapping->PostEditChange();
	L->NotifyModified();
}

EVisibility SQuestMappingDiscriminatorRow::GetRemoveVisibility() const
{
	const TSharedPtr<FQuestDiscriminatorRowItem> I = Item.Pin();
	// Only stale rows (stored but not in the sample) offer removal; in-sample rows are managed by the class picker alone.
	return (I.IsValid() && !I->bInSample) ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SQuestMappingDiscriminatorRow::OnRemoveClicked()
{
	const TSharedPtr<FQuestDiscriminatorRowItem> I = Item.Pin();
	const TSharedPtr<SQuestMappingDiscriminatorList> L = List.Pin();
	if (!I.IsValid() || !L.IsValid()) return FReply::Handled();
	UQuestImportMapping* Mapping = L->GetConfig().Mapping.Get();
	if (!Mapping) return FReply::Handled();

	const FScopedTransaction Transaction(LOCTEXT("RemoveDiscriminatorValue", "Remove Discriminator Value Mapping"));
	Mapping->Modify();
	const int32 Idx = FindEntryIndexForValue(*Mapping, I->Value);   // entry may hold the value under a different casing
	if (Idx != INDEX_NONE)
	{
		FQuestDiscriminatorClass& Entry = Mapping->DiscriminatorClasses[Idx];
		Entry.Values.RemoveAll([&](const FString& V){ return NormalizeDiscriminatorValue(V) == NormalizeDiscriminatorValue(I->Value); });
		if (Entry.Values.Num() == 0) { Mapping->DiscriminatorClasses.RemoveAt(Idx); }
		else if (NormalizeDiscriminatorValue(Entry.PrimaryValue) == NormalizeDiscriminatorValue(I->Value)) { Entry.PrimaryValue = Entry.Values[0]; }
	}
	Mapping->PostEditChange();
	L->NotifyModified();
	L->RefreshRows();   // the stale row is gone from both the map and the sample, so drop it from the list
	return FReply::Handled();
}

const FSlateBrush* SQuestMappingDiscriminatorRow::GetBorder() const
{
	// Draw the row's hovered brush whenever the cursor is over the row — unconditional, so it works under SelectionMode::None
	// (the base STableRow gates the hovered brush on selectability, which None disables). Falls back to the base otherwise.
	if (IsHovered())
	{
		const FTableRowStyle& RowStyle = FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
		return &RowStyle.EvenRowBackgroundHoveredBrush;
	}
	return SMultiColumnTableRow<FQuestDiscriminatorRowItemPtr>::GetBorder();
}

// ── List ───────────────────────────────────────────────────────────────────────────────────────────────────────────

void SQuestMappingDiscriminatorList::Construct(const FArguments& InArgs)
{
	Config = InArgs._Config;

	ChildSlot
	[
		SNew(SBox).MaxDesiredHeight(300.0f)
		[
			SAssignNew(ListView, SQuestDiscriminatorListView)
			.ListItemsSource(&Rows)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SQuestMappingDiscriminatorList::MakeRow)
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(ColumnId_Value).DefaultLabel(LOCTEXT("HdrValue", "Discriminator Value")).FillWidth(0.45f)
				+ SHeaderRow::Column(ColumnId_Class).DefaultLabel(LOCTEXT("HdrClass", "Node Class")).FillWidth(0.45f)
				+ SHeaderRow::Column(ColumnId_Remove).DefaultLabel(FText::GetEmpty()).FixedWidth(28.0f)
			)
		]
	];

	RefreshRows();
}

void SQuestMappingDiscriminatorList::RefreshRows()
{
	Rows.Reset();
	const UQuestImportMapping* Mapping = Config.Mapping.Get();
	if (Mapping && Config.DistinctValueProvider)
	{
		// Union rows: the sample's distinct values (first-seen order, marked in-sample) then any STORED key the sample lacks
		// (marked stale). A stored value present in the sample is one row (in-sample wins). This is what makes the recipe
		// outlive any single sample — a partial sample never silently drops a stored mapping. Dedup by NORMALIZED form so a
		// stored "Objective" and a sample "objective" collapse to one row (the guard treats them as the same value).
		const TArray<FString> SampleValues = Config.DistinctValueProvider();
		TSet<FString> AddedNorm;
		for (const FString& V : SampleValues)
		{
			const FString Norm = NormalizeDiscriminatorValue(V);
			if (AddedNorm.Contains(Norm)) continue;
			AddedNorm.Add(Norm);
			Rows.Add(FQuestDiscriminatorRowItem::Make(V, /*bInSample*/ true));
		}
		for (const FQuestDiscriminatorClass& Entry : Mapping->DiscriminatorClasses)
		{
			for (const FString& V : Entry.Values)
			{
				const FString Norm = NormalizeDiscriminatorValue(V);
				if (AddedNorm.Contains(Norm)) continue;   // already shown as an in-sample row (case-insensitively)
				AddedNorm.Add(Norm);
				Rows.Add(FQuestDiscriminatorRowItem::Make(V, /*bInSample*/ false));   // stored but not in sample = stale
			}
		}
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SQuestMappingDiscriminatorList::MakeRow(FQuestDiscriminatorRowItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SQuestMappingDiscriminatorRow, OwnerTable, Item.ToSharedRef(), SharedThis(this));
}

void SQuestMappingDiscriminatorList::NotifyModified()
{
	Config.OnMappingModified.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE

