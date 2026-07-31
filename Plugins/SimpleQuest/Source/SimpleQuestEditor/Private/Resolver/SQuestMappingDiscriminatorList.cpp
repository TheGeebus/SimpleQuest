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

	// Find the map key whose NORMALIZED form matches this row's value, so read/write/remove all target the exact stored key
	// rather than a differently-cased duplicate. The panel displays the raw value but keys the map the way the guard matches
	// (NormalizeDiscriminatorValue = trim+lower) — otherwise "Objective" and "objective" become two rows the guard then
	// rejects for a normalized-key collision. Returns the found key by out-param (true) or leaves it untouched (false).
	bool FindStoredKeyForValue(const UQuestImportMapping& Mapping, const FString& RowValue, FString& OutKey)
	{
		const FString Norm = NormalizeDiscriminatorValue(RowValue);
		for (const TPair<FString, TSoftClassPtr<UQuestlineNodeBase>>& Pair : Mapping.ClassByDiscriminatorValue)
		{
			if (NormalizeDiscriminatorValue(Pair.Key) == Norm) { OutKey = Pair.Key; return true; }
		}
		return false;
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
	// Match by normalized key (the map may store a differently-cased raw key than this row's displayed value).
	FString StoredKey;
	if (!FindStoredKeyForValue(*M, I->Value, StoredKey)) return nullptr;
	const TSoftClassPtr<UQuestlineNodeBase>* Entry = M->ClassByDiscriminatorValue.Find(StoredKey);
	// LoadSynchronously: SClassPropertyEntryBox.SelectedClass takes a resolved UClass*, and Get() returns null until the soft
	// class is otherwise loaded — so on reopen a mapped row would read as "None". One-time editor-only load on first display
	// (matches SGraphNode_QuestlineStep::GetObjectiveClass, which loads for the same reason).
	return Entry ? Entry->LoadSynchronous() : nullptr;
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
	// Reuse an existing key that normalizes to this row's value (update in place); else key a new entry by the normalized
	// form so the panel and the import guard agree from the first write — never store a raw-cased duplicate the guard rejects.
	FString StoredKey;
	const FString KeyToUse = FindStoredKeyForValue(*Mapping, I->Value, StoredKey) ? StoredKey : NormalizeDiscriminatorValue(I->Value);
	if (NewClass)
	{
		Mapping->ClassByDiscriminatorValue.Add(KeyToUse, TSoftClassPtr<UQuestlineNodeBase>(const_cast<UClass*>(NewClass)));
	}
	else
	{
		// Clearing to None removes the entry entirely (an absent entry == unmapped; keeps the map free of null-class rows).
		Mapping->ClassByDiscriminatorValue.Remove(KeyToUse);
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
	FString StoredKey;
	if (FindStoredKeyForValue(*Mapping, I->Value, StoredKey))   // remove the exact stored key (may differ in case from I->Value)
	{
		Mapping->ClassByDiscriminatorValue.Remove(StoredKey);
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
		for (const TPair<FString, TSoftClassPtr<UQuestlineNodeBase>>& Pair : Mapping->ClassByDiscriminatorValue)
		{
			const FString Norm = NormalizeDiscriminatorValue(Pair.Key);
			if (AddedNorm.Contains(Norm)) continue;   // already shown as an in-sample row (case-insensitively)
			AddedNorm.Add(Norm);
			Rows.Add(FQuestDiscriminatorRowItem::Make(Pair.Key, /*bInSample*/ false));   // stored but not in sample = stale
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

