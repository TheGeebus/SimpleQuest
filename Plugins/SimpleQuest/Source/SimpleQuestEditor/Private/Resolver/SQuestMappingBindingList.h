// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The binding-list widget for a UQuestImportMapping: one row per authored TARGET property of the mapped node classes, each
// with a source-column picker (a searchable dropdown of the source's ACTUAL columns — never free text, so a typo can't
// silently mis-bind) and an inline absent-field policy combo. Adapts the engine's chain-map list: the row anchors on the
// stable reflection side (our properties), the variable source column is the per-row selection. Parameterized by a config
// struct so it hardcodes nothing and can be embedded by the mapping asset editor now and a toolbar import flow later.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

class UQuestImportMapping;
class ITableRow;
class STableViewBase;
class SQuestMappingBindingList;

// One list row = one target property (the fixed anchor). The source column + policy are read/written live on the mapping's
// Bindings array, looked up by this property name — the element holds only the identity, never authored state (so a
// re-populate can't desync from the mapping).
class FQuestMappingRowItem
{
public:
	FName TargetProperty;
	FString PropertyTypeLabel;   // e.g. "FText", "bool" — a display hint so a designer picks the right source column

	static TSharedRef<FQuestMappingRowItem> Make(FName InProperty, const FString& InTypeLabel)
	{
		return MakeShareable(new FQuestMappingRowItem(InProperty, InTypeLabel));
	}

private:
	FQuestMappingRowItem(FName InProperty, const FString& InTypeLabel)
		: TargetProperty(InProperty), PropertyTypeLabel(InTypeLabel) {}
};

using FQuestMappingRowItemPtr = TSharedPtr<FQuestMappingRowItem>;

// How the widget reaches its mapping + source. TWeakObjectPtr so the widget never needs FGCObject; SourceColumnProvider is
// the provenance seam (returns the source's actual column names) — the widget calls it, never parses anything itself.
struct FQuestMappingBindingListConfig
{
	TWeakObjectPtr<UQuestImportMapping> Mapping;
	TFunction<TArray<FName>()> SourceColumnProvider;   // returns the source's actual columns (both provenances behind it)
	FSimpleDelegate OnMappingModified;                 // host marks the asset dirty / re-validates readiness

	bool IsValid() const { return Mapping.IsValid() && SourceColumnProvider; }
};

// One row widget: fixed property label | source-column searchable combo | inline policy combo.
class SQuestMappingBindingRow : public SMultiColumnTableRow<FQuestMappingRowItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SQuestMappingBindingRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable,
		TSharedRef<FQuestMappingRowItem> InItem, TSharedPtr<SQuestMappingBindingList> InList);

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	// Source-column combo: options are the provider's columns + a leading "None" (unmapped). String-backed, NOT FName —
	// Slate drops NAME_None from a combo, which would erase the unmapped state.
	TArray<TSharedPtr<FString>> SourceColumnOptions;
	void RebuildSourceColumnOptions();

	FText GetSelectedSourceColumn() const;                                          // reads the mapping's binding for this prop
	void OnSourceColumnChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type);    // writes it back

	FText GetPolicyText() const;
	TSharedRef<SWidget> BuildPolicyMenu();                                          // Preserve/Reset/Require
	void SetPolicy(uint8 NewPolicy);

	TWeakPtr<FQuestMappingRowItem> Item;
	TWeakPtr<SQuestMappingBindingList> List;
	friend class SQuestMappingBindingList;
};

using SQuestMappingListView = SListView<FQuestMappingRowItemPtr>;

class SQuestMappingBindingList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestMappingBindingList) {}
		SLATE_ARGUMENT(FQuestMappingBindingListConfig, Config)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Rebuild the row set from the mapping's current mapped node classes (their authored properties = the anchor rows).
	// Called on construct + whenever the mapping's classes/source change (the host triggers it).
	void RefreshRows();

	const FQuestMappingBindingListConfig& GetConfig() const { return Config; }
	void NotifyModified();   // fires Config.OnMappingModified

private:
	FQuestMappingBindingListConfig Config;
	TSharedPtr<SQuestMappingListView> ListView;
	TArray<FQuestMappingRowItemPtr> Rows;

	TSharedRef<ITableRow> MakeRow(FQuestMappingRowItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	friend class SQuestMappingBindingRow;
};