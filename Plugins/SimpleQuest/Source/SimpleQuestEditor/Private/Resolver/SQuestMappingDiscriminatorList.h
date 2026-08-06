// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The discriminator value->class list for a UQuestImportMapping: one row per distinct value of the mapping's discriminator
// column (read from the sample source), each with a node-class picker. The value labels are DISCOVERED from the source, never
// typed - closing the last corruption hole where the stock TMap editor let a designer hand-key the discriminator strings.
// Rows are the UNION of (distinct values in the current sample) + (values already stored in the class map); a stored value
// the sample doesn't contain still shows, flagged STALE + removable, so a partial sample never silently prunes a mapping.
// Parameterized by a config struct (mirrors SQuestMappingBindingList) so it hardcodes nothing and embeds in the asset editor
// now / a toolbar import flow later.
//
// Table mechanics - sorting, filtering, column widths, clipboard - come from SColumnTableView. What stays here is what a
// discriminator row MEANS. Note the stale signal deliberately does NOT rest on row position: sorting scatters the in-sample
// and stale blocks, so a count in the toolbar carries it instead, taken BEFORE the Show filter so it stays true whatever is
// hidden.

#include "CoreMinimal.h"
#include "Widgets/SColumnTableView.h"
#include "Widgets/SCompoundWidget.h"

class UQuestImportMapping;

// One list row = one discriminator VALUE (the fixed anchor). The class is read/written live on the mapping's
// DiscriminatorClasses entries, matched by this value's normalized form - the element holds only the identity and whether the
// value is present in the current sample (so a re-populate can't desync from the mapping, and stale rows render distinctly).
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

// How the widget reaches its mapping and the sample's distinct discriminator values. TWeakObjectPtr so no FGCObject needed;
// DistinctValueProvider returns the raw values found in the discriminator column of the current sample (the seam does the
// reading - the widget parses nothing). OnMappingModified lets the host mark dirty / re-validate.
struct FQuestMappingDiscriminatorListConfig
{
	TWeakObjectPtr<UQuestImportMapping> Mapping;
	TFunction<TArray<FString>()> DistinctValueProvider;   // raw distinct values of the discriminator column in the sample
	FSimpleDelegate OnMappingModified;

	bool IsValid() const { return Mapping.IsValid() && DistinctValueProvider; }
};

class SQuestMappingDiscriminatorList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestMappingDiscriminatorList) {}
		SLATE_ARGUMENT(FQuestMappingDiscriminatorListConfig, Config)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Rebuild the row set: union of the sample's distinct values and the map's stored keys (stored-but-absent = flagged
	 * stale), honouring the Show filter. Called on construct and whenever the sample source or the discriminator column
	 * changes; the host triggers those.
	 */
	void RefreshRows();

private:
	void NotifyModified();

	TArray<FTableColumnDef<FQuestDiscriminatorRowItemPtr>> MakeColumns();

	/**
	 * Cells, and the text each one is showing. The pairing matters: the cell is only APPEARANCE, and the text is what the
	 * table searches, sorts and copies. A class picker nothing can be searched by is the gap this closes.
	 */
	TSharedRef<SWidget> MakeValueCell(const FQuestDiscriminatorRowItemPtr& Item);
	TSharedRef<SWidget> MakeClassCell(const FQuestDiscriminatorRowItemPtr& Item);
	TSharedRef<SWidget> MakeRemoveCell(const FQuestDiscriminatorRowItemPtr& Item);

	/**
	 * These take the item BY VALUE, without exception. TDelegate decays its payload types, so a member bound with a payload
	 * cannot accept it by const reference. Several of these are reached both directly and through an attribute binding, and
	 * the binding is what dictates the parameter - so they are uniform rather than split by call style.
	 */
	FText GetValueText(FQuestDiscriminatorRowItemPtr Item) const;
	FText GetClassText(FQuestDiscriminatorRowItemPtr Item) const;
	const UClass* GetSelectedClass(FQuestDiscriminatorRowItemPtr Item) const;
	void OnSetClass(const UClass* NewClass, FQuestDiscriminatorRowItemPtr Item);
	EVisibility GetRemoveVisibility(FQuestDiscriminatorRowItemPtr Item) const;
	FReply OnRemoveClicked(FQuestDiscriminatorRowItemPtr Item);

	/** The table's live search text, so a custom cell can box its matched substring the way the default cell does. */
	FText GetTableFilterText() const;
	
	/**
	 * Row clipboard. The payload carries the class PATH, never the displayed name - a name cannot be resolved back to a
	 * class and two classes can share one. A magic first line lets a paste refuse clipboard content that is not ours
	 * rather than half-parsing something arbitrary into a recipe.
	 */
	void CopyRow(FQuestDiscriminatorRowItemPtr Item);
	void PasteRow(FQuestDiscriminatorRowItemPtr Item);
	bool CanPasteRow(FQuestDiscriminatorRowItemPtr Item);

	/** Hide values in the sample / hide values not in it - the "what has drifted" question this panel exists to answer. */
	TSharedRef<SWidget> BuildFilterMenu();
	FText GetStaleSummaryText() const;
	FSlateColor GetStaleSummaryColor() const;

	FQuestMappingDiscriminatorListConfig Config;
	TArray<FQuestDiscriminatorRowItemPtr> Rows;
	TSharedPtr<SColumnTableView<FQuestDiscriminatorRowItemPtr>> Table;

	/** Counted over the WHOLE union before the Show filter runs, which is what lets the summary survive being filtered. */
	int32 StaleCount = 0;
	int32 InSampleCount = 0;

	bool bHideInSample = false;
	bool bHideStale = false;
};