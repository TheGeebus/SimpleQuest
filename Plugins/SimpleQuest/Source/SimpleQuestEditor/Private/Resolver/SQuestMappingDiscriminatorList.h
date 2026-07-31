// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The discriminator value->class list for a UQuestImportMapping: one row per distinct value of the mapping's discriminator
// column (read from the sample source), each with a node-class picker. The value labels are DISCOVERED from the source, never
// typed — closing the last corruption hole where the stock TMap editor let a designer hand-key the discriminator strings.
// Rows are the UNION of (distinct values in the current sample) + (values already stored in the class map); a stored value
// the sample doesn't contain still shows, flagged STALE + removable, so a partial sample never silently prunes a mapping.
// Parameterized by a config struct (mirrors SQuestMappingBindingList) so it hardcodes nothing and embeds in the asset editor
// now / a toolbar import flow later.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

class UQuestImportMapping;
class ITableRow;
class STableViewBase;
class SQuestMappingDiscriminatorList;

// One list row = one discriminator VALUE (the fixed anchor). The class is read/written live on the mapping's
// ClassByDiscriminatorValue map, looked up by this raw value — the element holds only the identity + whether the value is
// present in the current sample (so a re-populate can't desync from the mapping, and stale rows render distinctly).
class FQuestDiscriminatorRowItem
{
public:
	FString Value;			// the raw discriminator value as authored in the source (the map key)
	bool bInSample = false;	// true = this value is present in the current sample source; false = stored-but-absent (stale)

	static TSharedRef<FQuestDiscriminatorRowItem> Make(const FString& InValue, bool bInInSample)
	{
		return MakeShareable(new FQuestDiscriminatorRowItem(InValue, bInInSample));
	}

private:
	FQuestDiscriminatorRowItem(const FString& InValue, bool bInInSample) : Value(InValue), bInSample(bInInSample) {}
};

using FQuestDiscriminatorRowItemPtr = TSharedPtr<FQuestDiscriminatorRowItem>;

// How the widget reaches its mapping + the sample's distinct discriminator values. TWeakObjectPtr so no FGCObject needed;
// DistinctValueProvider returns the raw values found in the discriminator column of the current sample (the seam does the
// reading — the widget parses nothing). OnMappingModified lets the host mark dirty / re-validate.
struct FQuestMappingDiscriminatorListConfig
{
	TWeakObjectPtr<UQuestImportMapping> Mapping;
	TFunction<TArray<FString>()> DistinctValueProvider;   // raw distinct values of the discriminator column in the sample
	FSimpleDelegate OnMappingModified;

	bool IsValid() const { return Mapping.IsValid() && DistinctValueProvider; }
};

// One row widget: value label (+ a "(not in sample)" note when stale) | node-class picker | remove-stale button.
class SQuestMappingDiscriminatorRow : public SMultiColumnTableRow<FQuestDiscriminatorRowItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SQuestMappingDiscriminatorRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable,
		TSharedRef<FQuestDiscriminatorRowItem> InItem, TSharedPtr<SQuestMappingDiscriminatorList> InList);

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	const UClass* GetSelectedClass() const;                  // reads the mapping's class-map entry for this value
	void OnSetClass(const UClass* NewClass);                 // writes it back (soft-ptr into the map)

	EVisibility GetRemoveVisibility() const;                 // remove button shows only for stale rows
	FReply OnRemoveClicked();                                // drops the stale entry from the map

	TWeakPtr<FQuestDiscriminatorRowItem> Item;
	TWeakPtr<SQuestMappingDiscriminatorList> List;
	friend class SQuestMappingDiscriminatorList;

	virtual const FSlateBrush* GetBorder() const override;   // whole-row hover highlight, independent of SelectionMode (base gates it off under None)
};

using SQuestDiscriminatorListView = SListView<FQuestDiscriminatorRowItemPtr>;

class SQuestMappingDiscriminatorList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestMappingDiscriminatorList) {}
		SLATE_ARGUMENT(FQuestMappingDiscriminatorListConfig, Config)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Rebuild the row set: union of the sample's distinct values + the map's stored keys (stored-but-absent = flagged stale).
	// Called on construct + whenever the sample source or the discriminator column changes (the host triggers it).
	void RefreshRows();

	const FQuestMappingDiscriminatorListConfig& GetConfig() const { return Config; }
	void NotifyModified();   // fires Config.OnMappingModified

private:
	FQuestMappingDiscriminatorListConfig Config;
	TSharedPtr<SQuestDiscriminatorListView> ListView;
	TArray<FQuestDiscriminatorRowItemPtr> Rows;

	TSharedRef<ITableRow> MakeRow(FQuestDiscriminatorRowItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	friend class SQuestMappingDiscriminatorRow;
};

