// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Details customization for UQuestImportMapping. The recipe describes a SHAPE, not where a file lives, so the panel gives a
// transient "sample source" control (editor-only, never serialized) whose columns + discriminator values feed the two
// bespoke pick-only widgets: the discriminator value->class list and the column->property binding list. Every authored field
// is a PICK from the sample — nothing is typed (the anti-corruption bar). Follows the module's customization pattern (see
// QuestlineGraphRewardsDetailsCustomization).

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class SQuestMappingBindingList;
class SQuestMappingDiscriminatorList;
class SSearchableComboBox;
class UQuestImportMapping;

class FQuestImportMappingDetailsCustomization : public IDetailCustomization, public FSelfRegisteringEditorUndoClient
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/**
	 * A transaction rollback restores the mapping asset directly and notifies nobody, so both list widgets keep the rows
	 * they built from the pre-undo state. The panel's own writes route through OnMappingModified, which an undo never
	 * reaches - these are the only hook that hears about it.
	 */
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	/** Narrows the above to transactions that actually touched OUR mapping; without it every editor undo rebuilds both lists. */
	virtual bool MatchesContext(const FTransactionContext& InContext,
		const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const override;

private:
private:
	/** Re-read both lists from the mapping. Shared by undo and redo. */
	void RefreshListsAfterTransaction();

	/**
	 * Copy/paste for the three picker rows. Without BOTH a CopyAction and a PasteAction the details panel treats a custom
	 * row as having no value and greys its whole context menu - FDetailWidgetRow::IsCopyPasteBound() requires the pair.
	 * Supplying them also lights up the stock Shift+RMB / Shift+LMB here, matching the tables below.
	 *
	 * No CanExecuteAction is wired, deliberately. SDetailSingleItemRow gates the paste CHORD on CanExecute, so a predicate
	 * would grey the menu entry at the cost of making a refused chord do nothing at all and say nothing - and a silent
	 * no-op is indistinguishable from a click that never registered. Each handler validates, reports, and returns.
	 */
	void CopySampleSource() const;
	void PasteSampleSource();

	/**
	 * Both column rows share one payload type on purpose: each holds a column name drawn from the same sample, so copying
	 * one into the other is something a designer actually wants rather than an accident to guard against.
	 */
	void CopyColumnValue(FName Column) const;
	void PasteDiscriminatorColumn();
	void PasteKeyColumn();

	/** The write, shared by the combo callback and the paste so both land identically. */
	void ApplyDiscriminatorColumn(FName Column);
	void ApplyKeyColumn(FName Column);

	/** True when the sample actually offers this column, or it is empty (meaning clear). Reads the CACHED option list. */
	bool IsPastableColumn(FName Column) const;
	
	TWeakObjectPtr<UQuestImportMapping> Mapping;
	TSharedPtr<SQuestMappingBindingList> BindingList;
	TSharedPtr<SQuestMappingDiscriminatorList> DiscriminatorList;

	// Transient sample source — editor-only, NEVER serialized on the recipe. Its job: give the two pick-only widgets real
	// columns + discriminator values to choose from, so a designer authors the SHAPE against a representative file. Thrown
	// away with the panel; the recipe stays shape-only (it describes a shape, not where a file lives).
	FName SampleFormatName = TEXT("TSV");
	FString SampleFolder;

	TArray<FName> SampleSourceColumns() const;					// feeds the binding widget's SourceColumnProvider
	TArray<FString> SampleDiscriminatorValues() const;			// feeds the discriminator widget's DistinctValueProvider
	TArray<FString> SampleQualifierOptions() const;				// structural pins + outcome identities from the sample's objective classes
	void OnMappingModified();

	// Sample-source control callbacks
	TArray<TSharedPtr<FString>> FormatOptions;					// registry names for the format dropdown
	FText GetSampleFormatText() const;
	void OnSampleFormatChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type);
	FText GetSampleFolderText() const;
	void OnSampleFolderCommitted(const FText& NewText, ETextCommit::Type);
	void RefreshFromSample();									// re-enumerate + tell BOTH widgets to rebuild

	// Discriminator-column picker: a dropdown of the sample's columns (never a typed FName).
	TArray<TSharedPtr<FString>> DiscriminatorColumnOptions;		// rebuilt from the sample's columns
	TSharedPtr<SSearchableComboBox> DiscriminatorColumnCombo;	// held so a sample change can RefreshOptions() it
	TSharedPtr<SSearchableComboBox> KeyColumnCombo;
	void RebuildDiscriminatorColumnOptions();
	FText GetDiscriminatorColumnText() const;					// reads Mapping->DiscriminatorColumn
	void OnDiscriminatorColumnChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type);
	
	// Key-column picker (reuses DiscriminatorColumnOptions — both pick a source column; never a typed FName).
	FText GetKeyColumnText() const;
	void OnKeyColumnChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type);

	void RestoreSampleFromMemo();   // per-user last-used sample for this recipe (never recipe data)
	void SaveSampleToMemo() const;
};

