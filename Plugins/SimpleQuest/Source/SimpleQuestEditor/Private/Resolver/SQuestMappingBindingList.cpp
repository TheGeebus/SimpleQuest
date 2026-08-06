// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/SQuestMappingBindingList.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ScopedTransaction.h"
#include "SimpleQuestLog.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestReflectionUtils.h"
#include "Nodes/QuestlineNodeBase.h"
#include "SSearchableComboBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
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

	/** The binding for a target property, or null. Const read helper. */
	const FQuestColumnBinding* FindBinding(const UQuestImportMapping* Mapping, FName TargetProperty)
	{
		if (!Mapping) return nullptr;
		return Mapping->Bindings.FindByPredicate([&](const FQuestColumnBinding& B) { return B.TargetProperty == TargetProperty; });
	}

	// Versioned so the payload can change later without a new build silently mis-reading old clipboard text.
	const FString ClipboardHeader(TEXT("SimpleQuestBindingRow/1"));

	/** Stable wire names for the policy - deliberately NOT the display strings, which are prose and will get reworded. */
	FString PolicyToWire(EQuestAbsentFieldPolicy P)
	{
		switch (P)
		{
		case EQuestAbsentFieldPolicy::Reset:   return TEXT("Reset");
		case EQuestAbsentFieldPolicy::Require: return TEXT("Require");
		case EQuestAbsentFieldPolicy::Preserve:
		default:                               return TEXT("Preserve");
		}
	}

	bool WireToPolicy(const FString& S, EQuestAbsentFieldPolicy& Out)
	{
		if (S == TEXT("Preserve")) { Out = EQuestAbsentFieldPolicy::Preserve; return true; }
		if (S == TEXT("Reset"))    { Out = EQuestAbsentFieldPolicy::Reset;    return true; }
		if (S == TEXT("Require"))  { Out = EQuestAbsentFieldPolicy::Require;  return true; }
		return false;
	}

	/** What a copied row carries. bBound is the field the display cannot express, which is the whole reason this exists. */
	struct FBindingRowPayload
	{
		bool bBound = false;
		FName SourceColumn = NAME_None;
		EQuestAbsentFieldPolicy AbsentPolicy = EQuestAbsentFieldPolicy::Preserve;
	};

	bool ParseClipboardPayload(const FString& Text, FBindingRowPayload& Out)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		if (Lines.Num() < 1 || Lines[0].TrimStartAndEnd() != ClipboardHeader) { return false; }

		bool bSawBound = false;
		FString PolicyWire;
		for (int32 i = 1; i < Lines.Num(); ++i)
		{
			FString Key, Value;
			if (!Lines[i].Split(TEXT("="), &Key, &Value)) { continue; }
			Key = Key.TrimStartAndEnd();
			Value = Value.TrimStartAndEnd();
			if      (Key == TEXT("Bound"))        { Out.bBound = (Value == TEXT("1")); bSawBound = true; }
			else if (Key == TEXT("SourceColumn")) { Out.SourceColumn = Value.IsEmpty() ? NAME_None : FName(*Value); }
			else if (Key == TEXT("AbsentPolicy")) { PolicyWire = Value; }
		}
		// A payload that never states existence is not ours whatever else it holds; a BOUND one must name a known policy.
		if (!bSawBound) { return false; }
		return !Out.bBound || WireToPolicy(PolicyWire, Out.AbsentPolicy);
	}
}

void SQuestMappingBindingList::Construct(const FArguments& InArgs)
{
	Config = InArgs._Config;

	// Per-ASSET persistence: a designer returning to a recipe should find the columns and sort they left it with. Keyed by
	// path rather than by widget instance, because what they think they are returning to is the recipe, not a panel.
	const UQuestImportMapping* Mapping = Config.Mapping.Get();
	const FString PersistKey = Mapping ? FString::Printf(TEXT("QuestMappingBindings.%s"), *Mapping->GetPathName()) : FString();

	ChildSlot
	[
		SNew(SBox).MaxDesiredHeight(400.0f)
		[
			SAssignNew(Table, SColumnTableView<FQuestMappingRowItemPtr>)
			.Columns(MakeColumns())
			// Nothing here acts on a selected row - you click a picker, not a row - so a persistent blue highlight is
			// noise, and a stale one is misleading. Hover survives; the shared row restores it explicitly.
			.SelectionMode(ESelectionMode::None)
			.PersistenceKey(PersistKey)
			.FilterHintText(LOCTEXT("FilterHint", "Filter properties and columns..."))
			.OnCopyRow(this, &SQuestMappingBindingList::CopyRow)
			.OnPasteRow(this, &SQuestMappingBindingList::PasteRow)
			.CanPasteRow(this, &SQuestMappingBindingList::CanPasteRow)
			.Toolbar()
			[
				SNew(SComboButton)
				.OnGetMenuContent(this, &SQuestMappingBindingList::BuildFilterMenu)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("FilterMenu", "Show"))
				]
			]
			.EmptyState()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const UQuestImportMapping* M = Config.Mapping.Get();
					if (!M || M->DiscriminatorClasses.IsEmpty())
					{
						return LOCTEXT("EmptyNoKinds", "No row kinds defined yet — add one above and its properties appear here.");
					}
					// Two different filters can empty this table. Test the search first: it is the more recent action, and rows
					// surviving into the table at all proves the Show filter left something for the search to hide.
					if (Table.IsValid() && Table->IsFilterActive())
					{
						return LOCTEXT("EmptyNoMatch", "No properties match the current search.");
					}
					if (bHideBound || bHideUnbound)
					{
						return LOCTEXT("EmptyAllFilteredOut", "Every property is hidden by the current Show filter.");
					}
					return LOCTEXT("EmptyNoProperties", "These row kinds expose no properties that can be bound.");
				})
			]
		]
	];

	RefreshFromSource();
}

TArray<FTableColumnDef<FQuestMappingRowItemPtr>> SQuestMappingBindingList::MakeColumns()
{
	TArray<FTableColumnDef<FQuestMappingRowItemPtr>> Cols;

	FTableColumnDef<FQuestMappingRowItemPtr> TargetCol;
	TargetCol.Id        = ColumnId_Target;
	TargetCol.Label     = LOCTEXT("HdrTarget", "Node Property");
	TargetCol.FillWidth = 0.35f;
	TargetCol.GetText   = [this](const FQuestMappingRowItemPtr& Item) { return GetTargetText(Item); };
	TargetCol.MakeCell  = [this](const FQuestMappingRowItemPtr& Item) { return MakeTargetCell(Item); };
	Cols.Add(MoveTemp(TargetCol));

	// The source column is a PICKER, and its selection is still what the table searches and sorts by. That pairing is the
	// whole reason this list can now answer "which property did I bind to 'reward_xp'?" - it never could before.
	FTableColumnDef<FQuestMappingRowItemPtr> SourceCol;
	SourceCol.Id        = ColumnId_Source;
	SourceCol.Label     = LOCTEXT("HdrSource", "Source Column");
	SourceCol.FillWidth = 0.40f;
	SourceCol.GetText   = [this](const FQuestMappingRowItemPtr& Item) { return GetSourceColumnText(Item); };
	SourceCol.MakeCell  = [this](const FQuestMappingRowItemPtr& Item) { return MakeSourceCell(Item); };
	Cols.Add(MoveTemp(SourceCol));

	FTableColumnDef<FQuestMappingRowItemPtr> PolicyCol;
	PolicyCol.Id        = ColumnId_Policy;
	PolicyCol.Label     = LOCTEXT("HdrPolicy", "If Absent");
	PolicyCol.FillWidth = 0.25f;
	PolicyCol.GetText   = [this](const FQuestMappingRowItemPtr& Item) { return GetPolicyText(Item); };
	PolicyCol.MakeCell  = [this](const FQuestMappingRowItemPtr& Item) { return MakePolicyCell(Item); };
	Cols.Add(MoveTemp(PolicyCol));

	return Cols;
}

// ── Cells ──────────────────────────────────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SQuestMappingBindingList::MakeTargetCell(const FQuestMappingRowItemPtr& Item)
{
	const FText Hint = Item.IsValid() ? FText::FromString(Item->PropertyTypeLabel) : FText::GetEmpty();
	return SNew(SBox).VAlign(VAlign_Center).Padding(3.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(GetTargetText(Item))
			.HighlightText(this, &SQuestMappingBindingList::GetTableFilterText)
			.Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
			.ToolTipText(FText::Format(LOCTEXT("PropTypeHint", "Type: {0}"), Hint))
		];
}

TSharedRef<SWidget> SQuestMappingBindingList::MakeSourceCell(const FQuestMappingRowItemPtr& Item)
{
	return SNew(SBox).VAlign(VAlign_Center).Padding(3.0f, 1.0f)
		[
			SNew(SSearchableComboBox)
			.OptionsSource(&SourceColumnOptions)
			// The combo drops a re-pick matching its own cached selection, and a paste or undo writes the binding without
			// going through it - so without this, re-selecting the pre-paste column would silently do nothing.
			.bAlwaysSelectItem(true)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(*In)); })
			.OnSelectionChanged(SComboBox<TSharedPtr<FString>>::FOnSelectionChanged::CreateSP(this, &SQuestMappingBindingList::OnSourceColumnChanged, Item))
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SQuestMappingBindingList::GetSourceColumnText, Item)))
				.HighlightText(this, &SQuestMappingBindingList::GetTableFilterText)
			]
		];
}

TSharedRef<SWidget> SQuestMappingBindingList::MakePolicyCell(const FQuestMappingRowItemPtr& Item)
{
	return SNew(SBox).VAlign(VAlign_Center).Padding(3.0f, 1.0f)
		[
			SNew(SComboButton)
			.OnGetMenuContent(FOnGetContent::CreateSP(this, &SQuestMappingBindingList::BuildPolicyMenu, Item))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SQuestMappingBindingList::GetPolicyText, Item)))
				.HighlightText(this, &SQuestMappingBindingList::GetTableFilterText)
			]
		];
}

// ── Values ─────────────────────────────────────────────────────────────────────────────────────────────────────────

FText SQuestMappingBindingList::GetTargetText(const FQuestMappingRowItemPtr& Item) const
{
	return Item.IsValid() ? FText::FromName(Item->TargetProperty) : FText::GetEmpty();
}

FText SQuestMappingBindingList::GetTableFilterText() const
{
	return Table.IsValid() ? Table->GetFilterText() : FText::GetEmpty();
}

FText SQuestMappingBindingList::GetSourceColumnText(FQuestMappingRowItemPtr Item) const
{
	if (!Item.IsValid()) return FText::FromString(NoneOption);
	const FQuestColumnBinding* B = FindBinding(Config.Mapping.Get(), Item->TargetProperty);
	return (B && !B->SourceColumn.IsNone()) ? FText::FromName(B->SourceColumn) : FText::FromString(NoneOption);
}

FText SQuestMappingBindingList::GetPolicyText(FQuestMappingRowItemPtr Item) const
{
	if (!Item.IsValid()) return PolicyDisplay(EQuestAbsentFieldPolicy::Preserve);
	const UQuestImportMapping* M = Config.Mapping.Get();
	const FQuestColumnBinding* B = FindBinding(M, Item->TargetProperty);
	// No binding = falls to the mapping's DefaultAbsentPolicy at import; show that so the row is honest about what applies.
	return PolicyDisplay(B ? B->AbsentPolicy : (M ? M->DefaultAbsentPolicy : EQuestAbsentFieldPolicy::Preserve));
}

bool SQuestMappingBindingList::IsBound(const FQuestMappingRowItemPtr& Item) const
{
	if (!Item.IsValid()) return false;
	const FQuestColumnBinding* B = FindBinding(Config.Mapping.Get(), Item->TargetProperty);
	return B && !B->SourceColumn.IsNone();
}

bool SQuestMappingBindingList::IsPastableSourceColumn(FName Column) const
{
	// A binding with no column is legal - setting a policy on an unmapped row creates exactly that.
	if (Column.IsNone()) { return true; }
	const FString AsText = Column.ToString();
	return SourceColumnOptions.ContainsByPredicate([&AsText](const TSharedPtr<FString>& Opt)
	{
		return Opt.IsValid() && *Opt != NoneOption && *Opt == AsText;
	});
}

void SQuestMappingBindingList::CopyRow(FQuestMappingRowItemPtr Item)
{
	if (!Item.IsValid()) return;
	const FQuestColumnBinding* B = FindBinding(Config.Mapping.Get(), Item->TargetProperty);

	FString Payload = ClipboardHeader + LINE_TERMINATOR;
	Payload += FString::Printf(TEXT("Bound=%d%s"), B ? 1 : 0, LINE_TERMINATOR);
	if (B)
	{
		const FString ColumnText = B->SourceColumn.IsNone() ? FString() : B->SourceColumn.ToString();
		Payload += FString::Printf(TEXT("SourceColumn=%s%s"), *ColumnText, LINE_TERMINATOR);
		Payload += FString::Printf(TEXT("AbsentPolicy=%s"), *PolicyToWire(B->AbsentPolicy));
	}
	FPlatformApplicationMisc::ClipboardCopy(*Payload);

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Binding row copied: '%s' -> %s."), *Item->TargetProperty.ToString(),
		B ? *FString::Printf(TEXT("column '%s', policy %s"),
				B->SourceColumn.IsNone() ? TEXT("(none)") : *B->SourceColumn.ToString(), *PolicyToWire(B->AbsentPolicy))
		  : TEXT("(unbound)"));
}

bool SQuestMappingBindingList::PasteRow(FQuestMappingRowItemPtr Item)
{
	if (!Item.IsValid()) return false;
	UQuestImportMapping* Mapping = Config.Mapping.Get();
	if (!Mapping) return false;

	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	FBindingRowPayload Payload;
	if (!ParseClipboardPayload(Text, Payload))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Binding paste onto '%s' refused: the clipboard does not hold a copied row."),
			*Item->TargetProperty.ToString());
		return false;
	}
	if (!IsPastableSourceColumn(Payload.SourceColumn))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Binding paste onto '%s' refused: column '%s' is not in the current sample."),
			*Item->TargetProperty.ToString(), *Payload.SourceColumn.ToString());
		return false;
	}

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Binding row pasting onto '%s': %s."), *Item->TargetProperty.ToString(),
		Payload.bBound ? *FString::Printf(TEXT("column '%s', policy %s"),
				Payload.SourceColumn.IsNone() ? TEXT("(none)") : *Payload.SourceColumn.ToString(), *PolicyToWire(Payload.AbsentPolicy))
		               : TEXT("(unbound)"));

	/**
	 * ONE transaction replacing the binding wholesale, rather than reusing the two pickers' write paths. Two writes would
	 * be two undo steps for a single gesture, and would pass through an intermediate "no column but Require" state that
	 * the import guard rejects. Remove-then-add has no intermediate state at all, and FQuestColumnBinding holds nothing
	 * beyond these three fields, so rewriting it entirely loses nothing.
	 */
	const FScopedTransaction Transaction(LOCTEXT("PasteBindingRow", "Paste Mapping Row Settings"));
	Mapping->Modify();
	Mapping->Bindings.RemoveAll([&](const FQuestColumnBinding& B) { return B.TargetProperty == Item->TargetProperty; });
	if (Payload.bBound)
	{
		FQuestColumnBinding NewBinding;
		NewBinding.TargetProperty = Item->TargetProperty;
		NewBinding.SourceColumn = Payload.SourceColumn;
		NewBinding.AbsentPolicy = Payload.AbsentPolicy;
		Mapping->Bindings.Add(MoveTemp(NewBinding));
	}
	Mapping->PostEditChange();
	NotifyModified();

	// Binding state feeds both the bound/unbound filter and the sort order, so the table has to re-evaluate.
	if (bHideBound || bHideUnbound) { RefreshRows(); }
	else if (Table.IsValid())       { Table->Refresh(); }
	return true;
}

bool SQuestMappingBindingList::CanPasteRow(FQuestMappingRowItemPtr Item)
{
	if (!Item.IsValid() || !Config.Mapping.IsValid()) { return false; }
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	FBindingRowPayload Payload;
	return ParseClipboardPayload(Text, Payload) && IsPastableSourceColumn(Payload.SourceColumn);
}

// ── Writes ─────────────────────────────────────────────────────────────────────────────────────────────────────────

void SQuestMappingBindingList::OnSourceColumnChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type, FQuestMappingRowItemPtr Item)
{
	if (!Item.IsValid() || !NewValue.IsValid()) return;
	UQuestImportMapping* Mapping = Config.Mapping.Get();
	if (!Mapping) return;

	const bool bUnmapped = (*NewValue == NoneOption);
	const FName NewCol = bUnmapped ? NAME_None : FName(**NewValue);

	const FScopedTransaction Transaction(LOCTEXT("SetSourceColumn", "Set Mapping Source Column"));
	Mapping->Modify();
	FQuestColumnBinding* Existing = Mapping->Bindings.FindByPredicate(
		[&](const FQuestColumnBinding& B) { return B.TargetProperty == Item->TargetProperty; });

	if (bUnmapped)
	{
		// Unmapping = remove the binding entirely (an absent binding == unmapped; keeps the array free of dead rows).
		if (Existing)
		{
			Mapping->Bindings.RemoveAll([&](const FQuestColumnBinding& B) { return B.TargetProperty == Item->TargetProperty; });
		}
	}
	else if (Existing)
	{
		Existing->SourceColumn = NewCol;
	}
	else
	{
		FQuestColumnBinding NewBinding;
		NewBinding.TargetProperty = Item->TargetProperty;
		NewBinding.SourceColumn = NewCol;
		// AbsentPolicy defaults to Preserve (the struct default).
		Mapping->Bindings.Add(MoveTemp(NewBinding));
	}
	Mapping->PostEditChange();   // sets the dirty flag + fires PostEditChangeProperty listeners (matches the Rewards customization precedent)
	NotifyModified();

	// Binding state feeds both the bound/unbound filter and the sort order, so the table has to re-evaluate.
	if (bHideBound || bHideUnbound) { RefreshRows(); }
	else if (Table.IsValid())       { Table->Refresh(); }
}

TSharedRef<SWidget> SQuestMappingBindingList::BuildPolicyMenu(FQuestMappingRowItemPtr Item)
{
	FMenuBuilder Menu(/*bCloseAfterSelection*/ true, nullptr);
	auto AddEntry = [&](EQuestAbsentFieldPolicy P)
	{
		Menu.AddMenuEntry(PolicyDisplay(P), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SQuestMappingBindingList::SetPolicy, static_cast<uint8>(P), Item)));
	};
	AddEntry(EQuestAbsentFieldPolicy::Preserve);
	AddEntry(EQuestAbsentFieldPolicy::Reset);
	AddEntry(EQuestAbsentFieldPolicy::Require);
	return Menu.MakeWidget();
}

void SQuestMappingBindingList::SetPolicy(uint8 NewPolicy, FQuestMappingRowItemPtr Item)
{
	if (!Item.IsValid()) return;
	UQuestImportMapping* Mapping = Config.Mapping.Get();
	if (!Mapping) return;

	const FScopedTransaction Transaction(LOCTEXT("SetAbsentPolicy", "Set Mapping Absent-Field Policy"));
	Mapping->Modify();
	FQuestColumnBinding* Existing = Mapping->Bindings.FindByPredicate(
		[&](const FQuestColumnBinding& B) { return B.TargetProperty == Item->TargetProperty; });
	if (Existing)
	{
		Existing->AbsentPolicy = static_cast<EQuestAbsentFieldPolicy>(NewPolicy);
	}
	else
	{
		// Setting a policy on an unmapped row creates the binding (unmapped column + a non-default policy is a state the
		// guard flags as contradictory - but authoring it is allowed; the guard reports it at validate/import time).
		FQuestColumnBinding NewBinding;
		NewBinding.TargetProperty = Item->TargetProperty;
		NewBinding.AbsentPolicy = static_cast<EQuestAbsentFieldPolicy>(NewPolicy);
		Mapping->Bindings.Add(MoveTemp(NewBinding));
	}
	Mapping->PostEditChange();
	NotifyModified();
	if (Table.IsValid()) { Table->Refresh(); }
}

// ── Filter menu ────────────────────────────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SQuestMappingBindingList::BuildFilterMenu()
{
	FMenuBuilder Menu(/*bCloseAfterSelection*/ false, nullptr);
	Menu.BeginSection(NAME_None, LOCTEXT("ShowSection", "Show"));

	Menu.AddMenuEntry(
		LOCTEXT("HideBound", "Hide bound properties"),
		LOCTEXT("HideBoundTip", "Show only properties with no source column yet — what is left to map."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bHideBound = !bHideBound; RefreshRows(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bHideBound; })),
		NAME_None, EUserInterfaceActionType::ToggleButton);

	Menu.AddMenuEntry(
		LOCTEXT("HideUnbound", "Hide unbound properties"),
		LOCTEXT("HideUnboundTip", "Show only properties that already have a source column."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bHideUnbound = !bHideUnbound; RefreshRows(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bHideUnbound; })),
		NAME_None, EUserInterfaceActionType::ToggleButton);

	Menu.EndSection();
	return Menu.MakeWidget();
}

// ── Population ─────────────────────────────────────────────────────────────────────────────────────────────────────

void SQuestMappingBindingList::RebuildSourceColumnOptions()
{
	SourceColumnOptions.Reset();
	SourceColumnOptions.Add(MakeShareable(new FString(NoneOption)));   // unmapped, always first
	if (Config.SourceColumnProvider)
	{
		for (const FName& Col : Config.SourceColumnProvider())
		{
			SourceColumnOptions.Add(MakeShareable(new FString(Col.ToString())));
		}
	}
	
	// This read parses every file in the sample folder. Logged so an unexpected cadence shows up as a pattern, not just as lag.
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Binding list: provider returned %d source column(s)."), SourceColumnOptions.Num() - 1);
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
		for (const FQuestDiscriminatorClass& Entry : Mapping->DiscriminatorClasses)
		{
			UClass* Cls = Entry.NodeClass.LoadSynchronous();
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
			FQuestMappingRowItemPtr Item = FQuestMappingRowItem::Make(N, PropToType[N]);
			// The bound/unbound filter is a POPULATION concern, not a text filter - the table's search box narrows what is
			// shown, this decides what exists to be shown at all.
			const bool bBound = IsBound(Item);
			if ((bBound && bHideBound) || (!bBound && bHideUnbound)) continue;
			Rows.Add(MoveTemp(Item));
		}
	}

	if (Table.IsValid())
	{
		Table->SetRootItems(Rows);
	}

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Binding list rebuilt: %d row(s) (HideBound=%d HideUnbound=%d)."),
		Rows.Num(),
		bHideBound ? 1 : 0,
		bHideUnbound ? 1 : 0);
}

void SQuestMappingBindingList::RefreshFromSource()
{
	RebuildSourceColumnOptions();
	RefreshRows();
}

void SQuestMappingBindingList::NotifyModified()
{
	Config.OnMappingModified.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE

