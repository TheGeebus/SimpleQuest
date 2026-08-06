// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/SQuestMappingDiscriminatorList.h"
#include "ScopedTransaction.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Nodes/QuestlineNodeBase.h"
#include "SimpleQuestLog.h"
#include "HAL/PlatformApplicationMisc.h"
#include "PropertyCustomizationHelpers.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SQuestMappingDiscriminatorList"

namespace
{
	const FName ColumnId_Value(TEXT("Value"));
	const FName ColumnId_Class(TEXT("Class"));
	const FName ColumnId_Remove(TEXT("Remove"));

	const FLinearColor StaleLabelColor(0.6f, 0.6f, 0.6f);
	const FLinearColor StaleNoteColor(0.8f, 0.6f, 0.3f);

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

	// Versioned so the payload can change later without a new build silently mis-reading old clipboard text.
	const FString ClipboardHeader(TEXT("SimpleQuestDiscriminatorRow/1"));

	/** Pull the class path out of a copied payload. False for anything that is not our format at all. */
	bool ParseClipboardClassPath(const FString& Text, FString& OutClassPath)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		if (Lines.Num() < 1 || Lines[0].TrimStartAndEnd() != ClipboardHeader) { return false; }
		for (int32 i = 1; i < Lines.Num(); ++i)
		{
			FString Key, Value;
			if (Lines[i].Split(TEXT("="), &Key, &Value) && Key.TrimStartAndEnd() == TEXT("Class"))
			{
				OutClassPath = Value.TrimStartAndEnd();
				return true;
			}
		}
		return false;
	}

	/**
	 * True when the path names a class the PICKER would have accepted - a paste must not reach a state the dropdown
	 * could not produce. An EMPTY path is valid and means "clear the class", which is why the class comes back through a
	 * parameter rather than as the return value.
	 */
	bool IsPastableClassPath(const FString& ClassPath, UClass*& OutClass)
	{
		OutClass = nullptr;
		if (ClassPath.IsEmpty()) { return true; }
		UClass* Cls = LoadClass<UQuestlineNodeBase>(nullptr, *ClassPath);
		if (!Cls || Cls->HasAnyClassFlags(CLASS_Abstract)) { return false; }
		OutClass = Cls;
		return true;
	}
}

void SQuestMappingDiscriminatorList::Construct(const FArguments& InArgs)
{
	Config = InArgs._Config;

	// Per-ASSET persistence, and a prefix distinct from the binding list's: both tables sit in the same details panel on the
	// same asset, and every table in the product shares one ini section, so a shared key would have them fighting.
	const UQuestImportMapping* Mapping = Config.Mapping.Get();
	const FString PersistKey = Mapping ? FString::Printf(TEXT("QuestMappingDiscriminators.%s"), *Mapping->GetPathName()) : FString();

	ChildSlot
	[
		SNew(SBox).MaxDesiredHeight(300.0f)
		[
			SAssignNew(Table, SColumnTableView<FQuestDiscriminatorRowItemPtr>)
			.Columns(MakeColumns())
			// Nothing here acts on a selected row - you click a picker, not a row - so a persistent blue highlight is
			// noise, and a stale one is misleading. Hover survives; the shared row restores it explicitly.
			.SelectionMode(ESelectionMode::None)
			.PersistenceKey(PersistKey)
			.FilterHintText(LOCTEXT("FilterHint", "Filter values and classes..."))
			.OnCopyRow(this, &SQuestMappingDiscriminatorList::CopyRow)
			.OnPasteRow(this, &SQuestMappingDiscriminatorList::PasteRow)
			.CanPasteRow(this, &SQuestMappingDiscriminatorList::CanPasteRow)
			.Toolbar()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &SQuestMappingDiscriminatorList::BuildFilterMenu)
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("FilterMenu", "Show"))
					]
				]
				// The drift signal. It reads the pre-filter count, so hiding rows never makes the panel look clean.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SQuestMappingDiscriminatorList::GetStaleSummaryText)
					.ColorAndOpacity(this, &SQuestMappingDiscriminatorList::GetStaleSummaryColor)
					.ToolTipText(LOCTEXT("StaleSummaryTip", "Values stored on this recipe that the current sample source does not contain. Use Show to isolate them."))
				]
			]
			.EmptyState()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const UQuestImportMapping* M = Config.Mapping.Get();
					if (!M || M->DiscriminatorColumn.IsNone())
					{
						return LOCTEXT("EmptyNoColumn", "Pick a discriminator column above and its values appear here.");
					}
					// Search first: it is the more recent action, and rows existing at all proves the Show filter left
					// something for the search to hide.
					if (Table.IsValid() && Table->IsFilterActive())
					{
						return LOCTEXT("EmptyNoMatch", "No values match the current search.");
					}
					if (bHideInSample || bHideStale)
					{
						return LOCTEXT("EmptyAllFilteredOut", "Every value is hidden by the current Show filter.");
					}
					return LOCTEXT("EmptyNoValues", "That column holds no values in this sample, and none are stored on the recipe.");
				})
			]
		]
	];

	RefreshRows();
}

TArray<FTableColumnDef<FQuestDiscriminatorRowItemPtr>> SQuestMappingDiscriminatorList::MakeColumns()
{
	TArray<FTableColumnDef<FQuestDiscriminatorRowItemPtr>> Cols;

	// The value carries no "(not in sample)" suffix in its TEXT even though the CELL shows one. Sorting, searching and the
	// clipboard all read this, and they should see the value the source actually holds - the Show menu isolates stale rows.
	FTableColumnDef<FQuestDiscriminatorRowItemPtr> ValueCol;
	ValueCol.Id        = ColumnId_Value;
	ValueCol.Label     = LOCTEXT("HdrValue", "Discriminator Value");
	ValueCol.FillWidth = 0.45f;
	ValueCol.GetText   = [this](const FQuestDiscriminatorRowItemPtr& Item) { return GetValueText(Item); };
	ValueCol.MakeCell  = [this](const FQuestDiscriminatorRowItemPtr& Item) { return MakeValueCell(Item); };
	Cols.Add(MoveTemp(ValueCol));

	// The class column had no text at all before, so nothing could search or sort by it. The picker's own display name is
	// what the row shows, so that is what it searches by - the two cannot disagree because both go through GetSelectedClass.
	FTableColumnDef<FQuestDiscriminatorRowItemPtr> ClassCol;
	ClassCol.Id        = ColumnId_Class;
	ClassCol.Label     = LOCTEXT("HdrClass", "Node Class");
	ClassCol.FillWidth = 0.45f;
	ClassCol.GetText   = [this](const FQuestDiscriminatorRowItemPtr& Item) { return GetClassText(Item); };
	ClassCol.MakeCell  = [this](const FQuestDiscriminatorRowItemPtr& Item) { return MakeClassCell(Item); };
	Cols.Add(MoveTemp(ClassCol));

	// An icon gutter: fixed, unsortable, unlabelled, and deliberately given no GetText so it contributes an empty clipboard
	// cell rather than a meaningless one.
	FTableColumnDef<FQuestDiscriminatorRowItemPtr> RemoveCol;
	RemoveCol.Id         = ColumnId_Remove;
	RemoveCol.Label      = FText::GetEmpty();
	RemoveCol.FixedWidth = 28.0f;
	RemoveCol.bSortable  = false;
	RemoveCol.MakeCell   = [this](const FQuestDiscriminatorRowItemPtr& Item) { return MakeRemoveCell(Item); };
	Cols.Add(MoveTemp(RemoveCol));

	return Cols;
}

// ── Cells ──────────────────────────────────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SQuestMappingDiscriminatorList::MakeValueCell(const FQuestDiscriminatorRowItemPtr& Item)
{
	// Stale values (stored but not in the current sample) render dimmed with a note, so a partial sample reads honestly.
	const bool bInSample = Item.IsValid() && Item->bInSample;
	const FText Note = bInSample ? FText::GetEmpty() : LOCTEXT("NotInSample", " (not in sample)");
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(GetValueText(Item))
			.HighlightText(this, &SQuestMappingDiscriminatorList::GetTableFilterText)
			.Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
			.ColorAndOpacity(bInSample ? FSlateColor::UseForeground() : FSlateColor(StaleLabelColor))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(Note)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.ItalicFont")))
			.ColorAndOpacity(FSlateColor(StaleNoteColor))
			.ToolTipText(LOCTEXT("StaleTip", "This value is stored on the recipe but is not present in the current sample source. Remove it, or point at a sample that contains it."))
		];
}

TSharedRef<SWidget> SQuestMappingDiscriminatorList::MakeClassCell(const FQuestDiscriminatorRowItemPtr& Item)
{
	return SNew(SBox).VAlign(VAlign_Center).Padding(3.0f, 1.0f)
		[
			SNew(SClassPropertyEntryBox)
			.MetaClass(UQuestlineNodeBase::StaticClass())
			.AllowNone(true)
			.AllowAbstract(false)
			.IsBlueprintBaseOnly(false)
			.SelectedClass(TAttribute<const UClass*>::Create(TAttribute<const UClass*>::FGetter::CreateSP(
				this, &SQuestMappingDiscriminatorList::GetSelectedClass, Item)))
			.OnSetClass(FOnSetClass::CreateSP(this, &SQuestMappingDiscriminatorList::OnSetClass, Item))
		];
}

TSharedRef<SWidget> SQuestMappingDiscriminatorList::MakeRemoveCell(const FQuestDiscriminatorRowItemPtr& Item)
{
	return SNew(SBox).VAlign(VAlign_Center).HAlign(HAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(
				this, &SQuestMappingDiscriminatorList::GetRemoveVisibility, Item)))
			.OnClicked(FOnClicked::CreateSP(this, &SQuestMappingDiscriminatorList::OnRemoveClicked, Item))
			.ToolTipText(LOCTEXT("RemoveStale", "Remove this stored mapping (its value isn't in the current sample)."))
			[
				SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))
			]
		];
}

// ── Values ─────────────────────────────────────────────────────────────────────────────────────────────────────────

FText SQuestMappingDiscriminatorList::GetValueText(FQuestDiscriminatorRowItemPtr Item) const
{
	return Item.IsValid() ? FText::FromString(Item->Value) : FText::GetEmpty();
}

FText SQuestMappingDiscriminatorList::GetClassText(FQuestDiscriminatorRowItemPtr Item) const
{
	const UClass* Cls = GetSelectedClass(Item);
	return Cls ? Cls->GetDisplayNameText() : FText::GetEmpty();
}

const UClass* SQuestMappingDiscriminatorList::GetSelectedClass(FQuestDiscriminatorRowItemPtr Item) const
{
	if (!Item.IsValid()) return nullptr;
	const UQuestImportMapping* M = Config.Mapping.Get();
	if (!M) return nullptr;
	const int32 Idx = FindEntryIndexForValue(*M, Item->Value);
	if (Idx == INDEX_NONE) return nullptr;
	// Loading is what the picker does anyway, and the result is cached - so text and picker can never disagree.
	return M->DiscriminatorClasses[Idx].NodeClass.LoadSynchronous();
}

FText SQuestMappingDiscriminatorList::GetTableFilterText() const
{
	return Table.IsValid() ? Table->GetFilterText() : FText::GetEmpty();
}

void SQuestMappingDiscriminatorList::CopyRow(FQuestDiscriminatorRowItemPtr Item)
{
	const UClass* Cls = GetSelectedClass(Item);
	const FString Payload = FString::Printf(TEXT("%s%sClass=%s"), *ClipboardHeader, LINE_TERMINATOR,
		Cls ? *Cls->GetPathName() : TEXT(""));
	FPlatformApplicationMisc::ClipboardCopy(*Payload);

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Discriminator row copied: '%s' -> %s."),
		Item.IsValid() ? *Item->Value : TEXT("<invalid>"), Cls ? *Cls->GetPathName() : TEXT("(no class)"));
}

bool SQuestMappingDiscriminatorList::PasteRow(FQuestDiscriminatorRowItemPtr Item)
{
	if (!Item.IsValid()) return false;

	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	FString ClassPath;
	UClass* Cls = nullptr;
	if (!ParseClipboardClassPath(Text, ClassPath) || !IsPastableClassPath(ClassPath, Cls))
	{
		UE_LOG(LogSimpleQuestResolver, Warning,
			TEXT("Discriminator paste onto '%s' refused: the clipboard does not hold a copied row."), *Item->Value);
		return false;
	}

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Discriminator row pasting onto '%s': %s."),
		*Item->Value,
		Cls ? *Cls->GetPathName() : TEXT("(cleared)"));

	// Straight through the picker's own write, so a paste cannot reach a state the dropdown could not - same transaction,
	// same normalized matching, same PrimaryValue repair. Announced BEFORE the write on purpose: the write cascades into
	// the binding list's own refresh log, so announcing afterwards prints the consequence above its cause.
	OnSetClass(Cls, Item);
	return true;
}

bool SQuestMappingDiscriminatorList::CanPasteRow(FQuestDiscriminatorRowItemPtr Item)
{
	if (!Item.IsValid() || !Config.Mapping.IsValid()) { return false; }
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	FString ClassPath;
	UClass* Cls = nullptr;
	return ParseClipboardClassPath(Text, ClassPath) && IsPastableClassPath(ClassPath, Cls);
}

// ── Writes ─────────────────────────────────────────────────────────────────────────────────────────────────────────

void SQuestMappingDiscriminatorList::OnSetClass(const UClass* NewClass, FQuestDiscriminatorRowItemPtr Item)
{
	if (!Item.IsValid()) return;
	UQuestImportMapping* Mapping = Config.Mapping.Get();
	if (!Mapping) return;

	const FScopedTransaction Transaction(LOCTEXT("SetDiscriminatorClass", "Set Discriminator Value Class"));
	Mapping->Modify();
	// Match the entry any of whose values normalizes to this row's value (update in place); else create a new entry keyed by
	// the normalized value, so the panel and the import guard agree from the first write - never store a raw-cased duplicate.
	const int32 Idx = FindEntryIndexForValue(*Mapping, Item->Value);
	if (NewClass)
	{
		if (Idx != INDEX_NONE)
		{
			// Value already belongs to an entry - repoint that entry's class (all its values move together, by design).
			Mapping->DiscriminatorClasses[Idx].NodeClass = TSoftClassPtr<UQuestlineNodeBase>(const_cast<UClass*>(NewClass));
		}
		else
		{
			// New value -> new entry keyed by the normalized value; PrimaryValue seeds to it (single-value entry).
			FQuestDiscriminatorClass NewEntry;
			NewEntry.NodeClass = TSoftClassPtr<UQuestlineNodeBase>(const_cast<UClass*>(NewClass));
			NewEntry.Values.Add(NormalizeDiscriminatorValue(Item->Value));
			NewEntry.PrimaryValue = NewEntry.Values[0];
			Mapping->DiscriminatorClasses.Add(MoveTemp(NewEntry));
		}
	}
	else if (Idx != INDEX_NONE)
	{
		// Clearing to None: drop this value from its entry; remove the entry if it becomes empty.
		FQuestDiscriminatorClass& Entry = Mapping->DiscriminatorClasses[Idx];
		Entry.Values.RemoveAll([&](const FString& V){ return NormalizeDiscriminatorValue(V) == NormalizeDiscriminatorValue(Item->Value); });
		if (Entry.PrimaryValue.IsEmpty() || NormalizeDiscriminatorValue(Entry.PrimaryValue) == NormalizeDiscriminatorValue(Item->Value))
		{
			Entry.PrimaryValue = Entry.Values.Num() > 0 ? Entry.Values[0] : FString();
		}
		if (Entry.Values.Num() == 0) { Mapping->DiscriminatorClasses.RemoveAt(Idx); }
	}
	Mapping->PostEditChange();
	NotifyModified();

	// Deliberately NOT a RefreshRows, matching the behaviour this replaces: clearing a stale row's class leaves the row
	// visible with an empty picker until the host refreshes. The class text feeds the sort, though, so the view re-reads.
	if (Table.IsValid()) { Table->Refresh(); }
}

EVisibility SQuestMappingDiscriminatorList::GetRemoveVisibility(FQuestDiscriminatorRowItemPtr Item) const
{
	// Only stale rows (stored but not in the sample) offer removal; in-sample rows are managed by the class picker alone.
	return (Item.IsValid() && !Item->bInSample) ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SQuestMappingDiscriminatorList::OnRemoveClicked(FQuestDiscriminatorRowItemPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();
	UQuestImportMapping* Mapping = Config.Mapping.Get();
	if (!Mapping) return FReply::Handled();

	const FScopedTransaction Transaction(LOCTEXT("RemoveDiscriminatorValue", "Remove Discriminator Value Mapping"));
	Mapping->Modify();
	const int32 Idx = FindEntryIndexForValue(*Mapping, Item->Value);   // entry may hold the value under a different casing
	if (Idx != INDEX_NONE)
	{
		FQuestDiscriminatorClass& Entry = Mapping->DiscriminatorClasses[Idx];
		Entry.Values.RemoveAll([&](const FString& V){ return NormalizeDiscriminatorValue(V) == NormalizeDiscriminatorValue(Item->Value); });
		if (Entry.Values.Num() == 0) { Mapping->DiscriminatorClasses.RemoveAt(Idx); }
		else if (NormalizeDiscriminatorValue(Entry.PrimaryValue) == NormalizeDiscriminatorValue(Item->Value)) { Entry.PrimaryValue = Entry.Values[0]; }
	}
	Mapping->PostEditChange();
	NotifyModified();
	RefreshRows();   // the stale row is gone from both the map and the sample, so drop it from the list
	return FReply::Handled();
}

// ── Filter menu ────────────────────────────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SQuestMappingDiscriminatorList::BuildFilterMenu()
{
	FMenuBuilder Menu(false, nullptr);
	Menu.BeginSection(NAME_None, LOCTEXT("ShowSection", "Show"));

	Menu.AddMenuEntry(
		LOCTEXT("HideInSample", "Hide values in the sample"),
		LOCTEXT("HideInSampleTip", "Show only values stored on the recipe that this sample no longer contains — what has drifted."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bHideInSample = !bHideInSample; RefreshRows(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bHideInSample; })),
		NAME_None, EUserInterfaceActionType::ToggleButton);

	Menu.AddMenuEntry(
		LOCTEXT("HideStale", "Hide values not in the sample"),
		LOCTEXT("HideStaleTip", "Show only values this sample actually contains."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bHideStale = !bHideStale; RefreshRows(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bHideStale; })),
		NAME_None, EUserInterfaceActionType::ToggleButton);

	Menu.EndSection();
	return Menu.MakeWidget();
}

FText SQuestMappingDiscriminatorList::GetStaleSummaryText() const
{
	if (StaleCount <= 0) { return FText::GetEmpty(); }
	FFormatNamedArguments Args;
	Args.Add(TEXT("Count"), StaleCount);
	return FText::Format(LOCTEXT("StaleSummary", "{Count} stored {Count}|plural(one=value,other=values) not in this sample"), Args);
}

FSlateColor SQuestMappingDiscriminatorList::GetStaleSummaryColor() const
{
	return FSlateColor(StaleNoteColor);
}

// ── Population ─────────────────────────────────────────────────────────────────────────────────────────────────────

void SQuestMappingDiscriminatorList::RefreshRows()
{
	Rows.Reset();
	StaleCount = 0;
	InSampleCount = 0;

	const UQuestImportMapping* Mapping = Config.Mapping.Get();
	if (Mapping && Config.DistinctValueProvider)
	{
		// The Show toggles are a POPULATION concern - the search box narrows what is shown, this decides what exists to be
		// shown at all. The counts are taken BEFORE the toggle, which is what keeps the summary honest while rows are hidden.
		auto AddRow = [this](const FString& V, bool bInSample)
		{
			if (bInSample) { ++InSampleCount; } else { ++StaleCount; }
			if ((bInSample && bHideInSample) || (!bInSample && bHideStale)) { return; }
			Rows.Add(FQuestDiscriminatorRowItem::Make(V, bInSample));
		};

		// Union rows: the sample's distinct values (first-seen order, marked in-sample) then any STORED key the sample lacks
		// (marked stale). A stored value present in the sample is one row (in-sample wins). This is what makes the recipe
		// outlive any single sample - a partial sample never silently drops a stored mapping. Dedup by NORMALIZED form so a
		// stored "Objective" and a sample "objective" collapse to one row (the guard treats them as the same value).
		const TArray<FString> SampleValues = Config.DistinctValueProvider();
		TSet<FString> AddedNorm;
		for (const FString& V : SampleValues)
		{
			const FString Norm = NormalizeDiscriminatorValue(V);
			if (AddedNorm.Contains(Norm)) continue;
			AddedNorm.Add(Norm);
			AddRow(V, true);
		}
		for (const FQuestDiscriminatorClass& Entry : Mapping->DiscriminatorClasses)
		{
			for (const FString& V : Entry.Values)
			{
				const FString Norm = NormalizeDiscriminatorValue(V);
				if (AddedNorm.Contains(Norm)) continue;   // already shown as an in-sample row (case-insensitively)
				AddedNorm.Add(Norm);
				AddRow(V, false);   // stored but not in sample = stale
			}
		}
	}

	if (Table.IsValid())
	{
		Table->SetRootItems(Rows);
	}

	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("Discriminator list rebuilt: %d row(s) shown, %d in sample, %d stale."),
		Rows.Num(),
		InSampleCount,
		StaleCount);
}

void SQuestMappingDiscriminatorList::NotifyModified()
{
	Config.OnMappingModified.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE