// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The binding-list widget for a UQuestImportMapping: one row per authored TARGET property of the mapped node classes, each
// with a source-column picker (a searchable dropdown of the source's ACTUAL columns - never free text, so a typo can't
// silently mis-bind) and an inline absent-field policy combo. Adapts the engine's chain-map list: the row anchors on the
// stable reflection side (our properties), the variable source column is the per-row selection. Parameterized by a config
// struct so it hardcodes nothing and can be embedded by the mapping asset editor now and a toolbar import flow later.
//
// Table mechanics - sorting, filtering, column widths, clipboard - come from SColumnTableView. This file supplies only
// what a binding row MEANS, which is why each column pairs a cell widget with the text value that cell is showing.

#include "CoreMinimal.h"
#include "Widgets/SColumnTableView.h"
#include "Widgets/SCompoundWidget.h"

class UQuestImportMapping;

/**
 * One list row = one target property (the fixed anchor). The source column and policy are read and written live on the
 * mapping's Bindings array, looked up by this property name - the element holds only the identity, never authored state,
 * so a re-populate cannot desync from the mapping.
 */
class FQuestMappingRowItem
{
public:
	FName TargetProperty;
	FString PropertyTypeLabel;   // e.g. "FText", "bool" - a display hint so a designer picks the right source column

	static TSharedRef<FQuestMappingRowItem> Make(FName InProperty, const FString& InTypeLabel)
	{
		return MakeShareable(new FQuestMappingRowItem(InProperty, InTypeLabel));
	}

private:
	FQuestMappingRowItem(FName InProperty, const FString& InTypeLabel)
		: TargetProperty(InProperty), PropertyTypeLabel(InTypeLabel) {}
};

using FQuestMappingRowItemPtr = TSharedPtr<FQuestMappingRowItem>;

/**
 * How the widget reaches its mapping and source. TWeakObjectPtr so the widget never needs FGCObject; SourceColumnProvider
 * is the provenance seam (returns the source's actual column names) - the widget calls it, never parses anything itself.
 */
struct FQuestMappingBindingListConfig
{
	TWeakObjectPtr<UQuestImportMapping> Mapping;
	TFunction<TArray<FName>()> SourceColumnProvider;   // returns the source's actual columns (both provenances behind it)
	FSimpleDelegate OnMappingModified;                 // host marks the asset dirty / re-validates readiness

	bool IsValid() const { return Mapping.IsValid() && SourceColumnProvider; }
};

class SQuestMappingBindingList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestMappingBindingList) {}
		SLATE_ARGUMENT(FQuestMappingBindingListConfig, Config)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Rebuild the row set from the mapping's current mapped node classes (their authored properties = the anchor rows),
	 * honouring the bound/unbound filter. Cheap: it does not re-read the source. Call whenever the mapped classes change.
	 */
	void RefreshRows();

	/**
	 * As RefreshRows, but re-asks the provider for the source's columns first. That read parses the sample folder, so it
	 * belongs only where the SOURCE changed - a different folder or format - never on an ordinary edit.
	 */
	void RefreshFromSource();

private:
	void NotifyModified();
	
	TArray<FTableColumnDef<FQuestMappingRowItemPtr>> MakeColumns();

	/**
	 * Cell widgets. Each is paired with a text accessor below, because the cell is only the APPEARANCE - the text is what
	 * the table searches, sorts and copies. A picker that nothing can search by its selection is the gap this closes.
	 */
	TSharedRef<SWidget> MakeTargetCell(const FQuestMappingRowItemPtr& Item);
	TSharedRef<SWidget> MakeSourceCell(const FQuestMappingRowItemPtr& Item);
	TSharedRef<SWidget> MakePolicyCell(const FQuestMappingRowItemPtr& Item);

	FText GetTargetText(const FQuestMappingRowItemPtr& Item) const;

	/** The table's live search text, so a custom cell can box its matched substring the way the default text cell does. */
	FText GetTableFilterText() const;

	/**
	 * Row clipboard. The payload states whether a binding EXISTS separately from its values, because the DISPLAY cannot:
	 * the source cell reads "None" both when no binding exists and when one exists pointing at no column, and the policy
	 * cell falls back to the mapping's default when unbound. Copying what is on screen would turn "this row is unbound"
	 * into "delete the target's policy", and "the default applies here" into an explicit override.
	 */
	void CopyRow(FQuestMappingRowItemPtr Item);
	bool PasteRow(FQuestMappingRowItemPtr Item);
	bool CanPasteRow(FQuestMappingRowItemPtr Item);

	/** A paste may not name a column the picker could not offer - that is the typo hole this panel exists to close. */
	bool IsPastableSourceColumn(FName Column) const;

	/**
	 * These two take the item BY VALUE, unlike their neighbours. TDelegate decays its payload types, so a member bound
	 * with a payload must accept that payload by value - a const reference will not match the bound signature. They are
	 * reached both directly and through an attribute binding, and the binding is what dictates the parameter.
	 */
	FText GetSourceColumnText(FQuestMappingRowItemPtr Item) const;
	FText GetPolicyText(FQuestMappingRowItemPtr Item) const;

	void OnSourceColumnChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type, FQuestMappingRowItemPtr Item);
	TSharedRef<SWidget> BuildPolicyMenu(FQuestMappingRowItemPtr Item);
	void SetPolicy(uint8 NewPolicy, FQuestMappingRowItemPtr Item);

	/** Hide Bound / Hide Unbound - the "what have I not mapped yet" question, which is why anyone scans this list. */
	TSharedRef<SWidget> BuildFilterMenu();

	/** Every row offers the SAME options (the source's columns), so one array serves the whole list. */
	void RebuildSourceColumnOptions();

	bool IsBound(const FQuestMappingRowItemPtr& Item) const;

	FQuestMappingBindingListConfig Config;
	TArray<FQuestMappingRowItemPtr> Rows;
	TArray<TSharedPtr<FString>> SourceColumnOptions;
	TSharedPtr<SColumnTableView<FQuestMappingRowItemPtr>> Table;

	bool bHideBound = false;
	bool bHideUnbound = false;
};