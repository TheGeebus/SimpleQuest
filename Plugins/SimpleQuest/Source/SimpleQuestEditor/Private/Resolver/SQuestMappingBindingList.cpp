// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/SQuestMappingBindingList.h"
#include "ScopedTransaction.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestReflectionUtils.h"
#include "Nodes/QuestlineNodeBase.h"
#include "SSearchableComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SQuestMappingBindingList"

namespace
{
	const FName ColumnId_Target(TEXT("TargetProperty"));
	const FName ColumnId_Source(TEXT("SourceColumn"));
	const FName ColumnId_Policy(TEXT("AbsentPolicy"));
	const FString NoneOption(TEXT("None"));   // the explicit unmapped entry (Slate drops NAME_None from a combo)

	FText PolicyDisplay(EQuestAbsentFieldPolicy P)
	{
		switch (P)
		{
		case EQuestAbsentFieldPolicy::Reset:   return LOCTEXT("PolReset", "Reset to default");
		case EQuestAbsentFieldPolicy::Require: return LOCTEXT("PolRequire", "Require");
		case EQuestAbsentFieldPolicy::Preserve:
		default:                               return LOCTEXT("PolPreserve", "Preserve graph value");
		}
	}
}

// ── Row ────────────────────────────────────────────────────────────────────────────────────────────────────────────

void SQuestMappingBindingRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable,
	TSharedRef<FQuestMappingRowItem> InItem, TSharedPtr<SQuestMappingBindingList> InList)
{
	Item = InItem;
	List = InList;
	RebuildSourceColumnOptions();
	SMultiColumnTableRow<FQuestMappingRowItemPtr>::Construct(FSuperRowType::FArguments().Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row")), InOwnerTable);
}

void SQuestMappingBindingRow::RebuildSourceColumnOptions()
{
	SourceColumnOptions.Reset();
	SourceColumnOptions.Add(MakeShareable(new FString(NoneOption)));   // unmapped, always first
	if (const TSharedPtr<SQuestMappingBindingList> L = List.Pin())
	{
		for (const FName& Col : L->GetConfig().SourceColumnProvider())
		{
			SourceColumnOptions.Add(MakeShareable(new FString(Col.ToString())));
		}
	}
}

TSharedRef<SWidget> SQuestMappingBindingRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == ColumnId_Target)
	{
		const TSharedPtr<FQuestMappingRowItem> I = Item.Pin();
		const FText Label = I.IsValid() ? FText::FromName(I->TargetProperty) : FText::GetEmpty();
		const FText Hint  = I.IsValid() ? FText::FromString(I->PropertyTypeLabel) : FText::GetEmpty();
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.0f, 1.0f)
			[
				SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
				.ToolTipText(FText::Format(LOCTEXT("PropTypeHint", "Type: {0}"), Hint))
			];
	}

	if (ColumnName == ColumnId_Source)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(3.0f, 1.0f)
			[
				SNew(SSearchableComboBox)
				.OptionsSource(&SourceColumnOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(*In)); })
				.OnSelectionChanged(this, &SQuestMappingBindingRow::OnSourceColumnChanged)
				[
					SNew(STextBlock).Text(this, &SQuestMappingBindingRow::GetSelectedSourceColumn)
				]
			];
	}

	if (ColumnName == ColumnId_Policy)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(3.0f, 1.0f)
			[
				SNew(SComboButton)
				.OnGetMenuContent(this, &SQuestMappingBindingRow::BuildPolicyMenu)
				.ButtonContent()
				[
					SNew(STextBlock).Text(this, &SQuestMappingBindingRow::GetPolicyText)
				]
			];
	}

	return SNullWidget::NullWidget;
}

// Find the binding for this row's target property (or null). Const read helper.
static const FQuestColumnBinding* FindBinding(const UQuestImportMapping* Mapping, FName TargetProperty)
{
	if (!Mapping) return nullptr;
	return Mapping->Bindings.FindByPredicate([&](const FQuestColumnBinding& B) { return B.TargetProperty == TargetProperty; });
}

FText SQuestMappingBindingRow::GetSelectedSourceColumn() const
{
	const TSharedPtr<FQuestMappingRowItem> I = Item.Pin();
	const TSharedPtr<SQuestMappingBindingList> L = List.Pin();
	if (!I.IsValid() || !L.IsValid()) return FText::FromString(NoneOption);
	const FQuestColumnBinding* B = FindBinding(L->GetConfig().Mapping.Get(), I->TargetProperty);
	return (B && !B->SourceColumn.IsNone()) ? FText::FromName(B->SourceColumn) : FText::FromString(NoneOption);
}

void SQuestMappingBindingRow::OnSourceColumnChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type)
{
	const TSharedPtr<FQuestMappingRowItem> I = Item.Pin();
	const TSharedPtr<SQuestMappingBindingList> L = List.Pin();
	if (!I.IsValid() || !L.IsValid() || !NewValue.IsValid()) return;
	UQuestImportMapping* Mapping = L->GetConfig().Mapping.Get();
	if (!Mapping) return;

	const bool bUnmapped = (*NewValue == NoneOption);
	const FName NewCol = bUnmapped ? NAME_None : FName(**NewValue);

	const FScopedTransaction Transaction(LOCTEXT("SetSourceColumn", "Set Mapping Source Column"));
	Mapping->Modify();
	FQuestColumnBinding* Existing = Mapping->Bindings.FindByPredicate(
		[&](const FQuestColumnBinding& B) { return B.TargetProperty == I->TargetProperty; });

	if (bUnmapped)
	{
		// Unmapping = remove the binding entirely (an absent binding == unmapped; keeps the array free of dead rows).
		if (Existing)
		{
			Mapping->Bindings.RemoveAll([&](const FQuestColumnBinding& B) { return B.TargetProperty == I->TargetProperty; });
		}
	}
	else if (Existing)
	{
		Existing->SourceColumn = NewCol;
	}
	else
	{
		FQuestColumnBinding NewBinding;
		NewBinding.TargetProperty = I->TargetProperty;
		NewBinding.SourceColumn = NewCol;
		// AbsentPolicy defaults to Preserve (the struct default).
		Mapping->Bindings.Add(MoveTemp(NewBinding));
	}
	Mapping->PostEditChange();   // sets the dirty flag + fires PostEditChangeProperty listeners (matches the Rewards customization precedent)
	L->NotifyModified();
}

FText SQuestMappingBindingRow::GetPolicyText() const
{
	const TSharedPtr<FQuestMappingRowItem> I = Item.Pin();
	const TSharedPtr<SQuestMappingBindingList> L = List.Pin();
	if (!I.IsValid() || !L.IsValid()) return PolicyDisplay(EQuestAbsentFieldPolicy::Preserve);
	const FQuestColumnBinding* B = FindBinding(L->GetConfig().Mapping.Get(), I->TargetProperty);
	// No binding = falls to the mapping's DefaultAbsentPolicy at import; show that so the row is honest about what applies.
	const UQuestImportMapping* M = L->GetConfig().Mapping.Get();
	return PolicyDisplay(B ? B->AbsentPolicy : (M ? M->DefaultAbsentPolicy : EQuestAbsentFieldPolicy::Preserve));
}

TSharedRef<SWidget> SQuestMappingBindingRow::BuildPolicyMenu()
{
	FMenuBuilder Menu(/*bCloseAfterSelection*/ true, nullptr);
	auto AddEntry = [&](EQuestAbsentFieldPolicy P)
	{
		Menu.AddMenuEntry(PolicyDisplay(P), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SQuestMappingBindingRow::SetPolicy, static_cast<uint8>(P))));
	};
	AddEntry(EQuestAbsentFieldPolicy::Preserve);
	AddEntry(EQuestAbsentFieldPolicy::Reset);
	AddEntry(EQuestAbsentFieldPolicy::Require);
	return Menu.MakeWidget();
}

void SQuestMappingBindingRow::SetPolicy(uint8 NewPolicy)
{
	const TSharedPtr<FQuestMappingRowItem> I = Item.Pin();
	const TSharedPtr<SQuestMappingBindingList> L = List.Pin();
	if (!I.IsValid() || !L.IsValid()) return;
	UQuestImportMapping* Mapping = L->GetConfig().Mapping.Get();
	if (!Mapping) return;

	const FScopedTransaction Transaction(LOCTEXT("SetAbsentPolicy", "Set Mapping Absent-Field Policy"));
	Mapping->Modify();
	FQuestColumnBinding* Existing = Mapping->Bindings.FindByPredicate(
		[&](const FQuestColumnBinding& B) { return B.TargetProperty == I->TargetProperty; });
	if (Existing)
	{
		Existing->AbsentPolicy = static_cast<EQuestAbsentFieldPolicy>(NewPolicy);
	}
	else
	{
		// Setting a policy on an unmapped row creates the binding (unmapped column + a non-default policy is a state the
		// guard flags as contradictory — but authoring it is allowed; the guard reports it at validate/import time).
		FQuestColumnBinding NewBinding;
		NewBinding.TargetProperty = I->TargetProperty;
		NewBinding.AbsentPolicy = static_cast<EQuestAbsentFieldPolicy>(NewPolicy);
		Mapping->Bindings.Add(MoveTemp(NewBinding));
	}
	Mapping->PostEditChange();
	L->NotifyModified();
}

const FSlateBrush* SQuestMappingBindingRow::GetBorder() const
{
	// Draw the row's hovered brush whenever the cursor is over the row — unconditional, so it works under SelectionMode::None
	// (the base STableRow gates the hovered brush on selectability, which None disables). Falls back to the base otherwise.
	if (IsHovered())
	{
		const FTableRowStyle& RowStyle = FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
		return &RowStyle.EvenRowBackgroundHoveredBrush;
	}
	return SMultiColumnTableRow<FQuestMappingRowItemPtr>::GetBorder();
}

// ── List ───────────────────────────────────────────────────────────────────────────────────────────────────────────

void SQuestMappingBindingList::Construct(const FArguments& InArgs)
{
	Config = InArgs._Config;

	ChildSlot
	[
		SNew(SBox).MaxDesiredHeight(400.0f)
		[
			SAssignNew(ListView, SQuestMappingListView)
			.ListItemsSource(&Rows)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SQuestMappingBindingList::MakeRow)
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(ColumnId_Target).DefaultLabel(LOCTEXT("HdrTarget", "Node Property")).FillWidth(0.35f)
				+ SHeaderRow::Column(ColumnId_Source).DefaultLabel(LOCTEXT("HdrSource", "Source Column")).FillWidth(0.40f)
				+ SHeaderRow::Column(ColumnId_Policy).DefaultLabel(LOCTEXT("HdrPolicy", "If Absent")).FillWidth(0.25f)
			)
		]
	];

	RefreshRows();
}

void SQuestMappingBindingList::RefreshRows()
{
	Rows.Reset();
	const UQuestImportMapping* Mapping = Config.Mapping.Get();
	if (Mapping)
	{
		// Anchor rows = the UNION of authored properties across every mapped node class (bind once, applies where it fits).
		// Dedup by name; a property on multiple classes is one row. Carry a type-label hint from the first class that has it.
		TMap<FName, FString> PropToType;
		for (const TPair<FString, TSoftClassPtr<UQuestlineNodeBase>>& Pair : Mapping->ClassByDiscriminatorValue)
		{
			UClass* Cls = Pair.Value.LoadSynchronous();
			if (!Cls) continue;
			for (const FName& PropName : GetAuthoredPropertyNames(Cls))
			{
				if (PropToType.Contains(PropName)) continue;
				FString TypeLabel;
				if (const FProperty* P = Cls->FindPropertyByName(PropName))
				{
					TypeLabel = P->GetCPPType();
				}
				PropToType.Add(PropName, TypeLabel);
			}
		}
		TArray<FName> SortedNames;
		PropToType.GetKeys(SortedNames);
		SortedNames.Sort(FNameLexicalLess());
		for (const FName& N : SortedNames)
		{
			Rows.Add(FQuestMappingRowItem::Make(N, PropToType[N]));
		}
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SQuestMappingBindingList::MakeRow(FQuestMappingRowItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SQuestMappingBindingRow, OwnerTable, Item.ToSharedRef(), SharedThis(this));
}

void SQuestMappingBindingList::NotifyModified()
{
	Config.OnMappingModified.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE