// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Resolver/QuestInPlacePlan.h"
#include "Resolver/QuestPlanBroker.h"
#include "SWarningOrErrorBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SColumnTableView.h"
#include "Widgets/SCompoundWidget.h"

class UQuestlineGraph;


DECLARE_DELEGATE_OneParam(FOnQuestPlanFormatChanged, FString /*FormatName*/);
DECLARE_DELEGATE_OneParam(FOnQuestPlanMappingChanged, const FSoftObjectPath& /*MappingAsset*/);
DECLARE_DELEGATE_OneParam(FOnQuestPlanFolderChanged, const FString& /*Folder*/);
DECLARE_DELEGATE_OneParam(FOnQuestPlanTableChanged, const FSoftObjectPath& /*SourceTable*/);
DECLARE_DELEGATE_OneParam(FOnQuestPlanSourceKindChanged, EQuestPlanSourceKind /*Kind*/);

/** Which end of the pipeline a row describes. The controls are identical; the WORDS are not. */
enum class EQuestEndpointRole : uint8 { Source, Destination };

/**
 * Everything one endpoint row needs to describe a place data lives. It exists so the SOURCE row and the DESTINATION row
 * can be the SAME row rather than a copy of it - the layout decisions inside were each argued once and should not have
 * to be argued again per direction.
 *
 * Kind and FormatCombo are POINTERS to the owning panel's members, because a row drives LIVE panel state rather than
 * holding its own: the panel seeds the kind at construction and reads it back when deciding what to show.
 */
struct FQuestEndpointRowArgs
{
	FText Label;

	EQuestPlanSourceKind* Kind = nullptr;
	TSharedPtr<SComboBox<TSharedPtr<FString>>>* FormatCombo = nullptr;
	EQuestEndpointRole Role = EQuestEndpointRole::Source;

	TAttribute<FString>         Folder;
	TAttribute<FSoftObjectPath> Table;
	TAttribute<FString>         FormatName;
	TAttribute<FSoftObjectPath> Mapping;

	FOnQuestPlanFolderChanged     OnFolderChanged;
	FOnQuestPlanTableChanged      OnTableChanged;
	FOnQuestPlanSourceKindChanged OnKindChanged;
	FOnQuestPlanFormatChanged     OnFormatChanged;
	FOnQuestPlanMappingChanged    OnMappingChanged;
	FSimpleDelegate               OnBrowseRequested;
};

/**
 * One line in the plan tree. A plan has two levels and they are genuinely different things - a NODE that would change,
 * and the individual PROPERTIES that would change on it - so rather than flatten them into one row shape with half the
 * fields blank, a row knows which it is and the columns ask it.
 */
struct FQuestPlanRow
{
	enum class EKind : uint8 { Node, Change };

	EKind   Kind = EKind::Node;
	FString Action;      // CREATE / MOVE / UPDATE / ORPHAN — blank on a change row
	FString Name;        // the node's label (or key), or the property's name
	FString Detail;      // where the node lives, or "before -> after" for a property
	bool    bStructural = false;

	TArray<TSharedPtr<FQuestPlanRow>> Children;
};

using FQuestPlanRowPtr = TSharedPtr<FQuestPlanRow>;

/**
 * Shows what a re-import WOULD do to the questline this editor has open. Display only - it never computes or applies a
 * plan, which is what lets the console today and a toolbar button tomorrow feed the same surface.
 */
class SQuestPlanPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestPlanPanel) {}
		/** The asset this panel speaks for. Plans for any other asset are ignored - a plan is about one questline. */
		SLATE_ARGUMENT(FString, TargetAssetPath)

		/**
		 * The asset itself, so the panel can notice when it changes underneath a plan. A plan is a COMPARISON against a
		 * particular asset state; the moment that state moves, the plan stops being true, and a panel still displaying it
		 * is worse than an empty one because it looks authoritative.
		 */
		SLATE_ARGUMENT(TWeakObjectPtr<UQuestlineGraph>, Questline)

		SLATE_EVENT(FSimpleDelegate, OnBuildPlanRequested)

		/** Raised by Apply. Same reason - the toolkit owns the transaction and the source, the panel owns the display. */
		SLATE_EVENT(FSimpleDelegate, OnApplyRequested)

		/** Raised by the Browse button. The panel displays a path; the toolkit owns the dialog. */
		SLATE_EVENT(FSimpleDelegate, OnBrowseRequested)

		/**
		 * PROVENANCE - what the displayed plan was built from, which is NOT the same fact as what is currently selected.
		 * Empty when no plan exists, and the subtitle slot collapses. The selection lives in the fields below, and the
		 * two disagreeing is what SourceStale reports.
		 */
		SLATE_ATTRIBUTE(FText, PlanProvenance)

		/** SELECTION - the folder the next Build Plan will read. Editable; typing and browsing converge on one write. */
		SLATE_ATTRIBUTE(FString, SourceFolder)
		SLATE_EVENT(FOnQuestPlanFolderChanged, OnFolderChanged)

		/** SELECTION - the Data Table the next Build Plan will read. Mutually exclusive with the folder. */
		SLATE_ATTRIBUTE(FSoftObjectPath, SourceTable)
		SLATE_EVENT(FOnQuestPlanTableChanged, OnTableChanged)

		/** Raised when the designer switches provenance. The owner clears whichever side is no longer in play. */
		SLATE_EVENT(FOnQuestPlanSourceKindChanged, OnSourceKindChanged)

		/** The format provider the next read will use. Shown in the combo; the list itself comes from the registry. */
		SLATE_ATTRIBUTE(FString, FormatName)

		/** Raised when the designer picks a different format. Changes a SELECTION only - nothing is read until Build Plan. */
		SLATE_EVENT(FOnQuestPlanFormatChanged, OnFormatChanged)

		/** The translation mapping, or an empty path for none - meaning the source is already in our own shape. */
		SLATE_ATTRIBUTE(FSoftObjectPath, MappingAsset)

		/** Raised when the designer picks or clears a mapping. */
		SLATE_EVENT(FOnQuestPlanMappingChanged, OnMappingChanged)

		/** Whether Build Plan can run - a source that resolves. Distinct from CanApply, which needs a clean plan. */
		SLATE_ATTRIBUTE(bool, CanBuildPlan)

		/** Whether Apply is currently permitted. Bound rather than pushed, so it re-evaluates as plans come and go. */
		SLATE_ATTRIBUTE(bool, CanApply)

		/**
		 * True when the SELECTION has moved away from what the displayed plan was built from. A second staleness
		 * reason alongside the asset having changed, and it wants its own sentence.
		 */
		SLATE_ATTRIBUTE(bool, SourceStale)

		
		/** Raised by Export. The panel displays; the toolkit owns the operation and the destination. */
		SLATE_EVENT(FSimpleDelegate, OnExportRequested)

		/** Whether Export applies. False for a Data Table source - that direction is a reverse-apply, not an export. */
		SLATE_ATTRIBUTE(bool, CanExport)

		/**
		 * THE DESTINATION - where writing OUT goes, which is a genuinely different question from where reading comes
		 * FROM. One endpoint could not express "import a studio's table, export readable files", and that is a real
		 * workflow. Same shape as the source bindings above, because a destination IS an endpoint.
		 */
		SLATE_ATTRIBUTE(FString, DestinationFolder)
		SLATE_EVENT(FOnQuestPlanFolderChanged, OnDestinationFolderChanged)

		SLATE_ATTRIBUTE(FSoftObjectPath, DestinationTable)
		SLATE_EVENT(FOnQuestPlanTableChanged, OnDestinationTableChanged)

		SLATE_EVENT(FOnQuestPlanSourceKindChanged, OnDestinationKindChanged)

		SLATE_ATTRIBUTE(FString, DestinationFormatName)
		SLATE_EVENT(FOnQuestPlanFormatChanged, OnDestinationFormatChanged)

		SLATE_ATTRIBUTE(FSoftObjectPath, DestinationMapping)
		SLATE_EVENT(FOnQuestPlanMappingChanged, OnDestinationMappingChanged)

		SLATE_EVENT(FSimpleDelegate, OnDestinationBrowseRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SQuestPlanPanel() override;

private:
	void HandlePlanCleared(const FString& InAssetPath);	
	void HandlePlanPublished(const FString& InAssetPath, const FQuestInPlacePlan& Plan);
	void RebuildRows(const FQuestInPlacePlan& Plan);
	TArray<FTableColumnDef<FQuestPlanRowPtr>> MakeColumns() const;

	FText GetSummaryText() const;
	FText GetBlockersText() const;
	EMessageStyle GetBlockersStyle() const;
	EVisibility GetBlockersVisibility() const;
	EVisibility GetStaleVisibility() const;
	FText GetStaleText() const;
	/** One endpoint row, built from bindings rather than from members, so it can be built more than once. */
	TSharedRef<SWidget> MakeEndpointRow(const FQuestEndpointRowArgs& Args);
	void HandleObjectModified(UObject* Modified);

	FString TargetAssetPath;
	TWeakObjectPtr<UQuestlineGraph> Questline;
	FDelegateHandle PublishHandle;
	FDelegateHandle ModifiedHandle;
	FDelegateHandle ClearedHandle;

	FSimpleDelegate OnBuildPlanRequested;
	FSimpleDelegate OnApplyRequested;
	FSimpleDelegate OnBrowseRequested;
	TAttribute<bool> CanBuildPlan;
	TAttribute<bool> CanApply;
	TAttribute<bool> SourceStale;

	FSimpleDelegate OnExportRequested;
	TAttribute<bool> CanExport;

	void HandleExportCompleted(const FString& InAssetPath, const FString& InSummary, const FString& Error);
	FDelegateHandle ExportHandle;

	/**
	 * The last export attempt, shown until the next action replaces it. TRANSIENT by design - a receipt is consumed by
	 * whoever asked for it, so nothing stores it. The error shares the blockers banner rather than adding a third bar:
	 * both answer "why can I not get on with this", and a panel that grows a bar per failure kind loses the plan.
	 */
	FString LastExportSummary;
	FString LastExportError;

	TAttribute<FText> PlanProvenance;
	TAttribute<FString> SourceFolder;
	FOnQuestPlanFolderChanged OnFolderChanged;
	TAttribute<FSoftObjectPath> SourceTable;
	FOnQuestPlanTableChanged OnTableChanged;
	FOnQuestPlanSourceKindChanged OnSourceKindChanged;
	TAttribute<FString> FormatName;
	FOnQuestPlanFormatChanged OnFormatChanged;
	TAttribute<FSoftObjectPath> MappingAsset;
	FOnQuestPlanMappingChanged OnMappingChanged;
	TAttribute<FString> DestinationFolder;
	FOnQuestPlanFolderChanged OnDestinationFolderChanged;
	TAttribute<FSoftObjectPath> DestinationTable;
	FOnQuestPlanTableChanged OnDestinationTableChanged;
	FOnQuestPlanSourceKindChanged OnDestinationKindChanged;
	TAttribute<FString> DestinationFormatName;
	FOnQuestPlanFormatChanged OnDestinationFormatChanged;
	TAttribute<FSoftObjectPath> DestinationMapping;
	FOnQuestPlanMappingChanged OnDestinationMappingChanged;
	FSimpleDelegate OnDestinationBrowseRequested;

	/** Which provenance the DESTINATION row is showing - its own state, for the same reason the source's is. */
	EQuestPlanSourceKind ShownDestinationKind = EQuestPlanSourceKind::Folder;

	/** Per-row, because there are two combos now. FormatOptions stays SHARED - the registry is one list. */
	TSharedPtr<SComboBox<TSharedPtr<FString>>> DestinationFormatCombo;

	/**
	 * Which provenance the row is SHOWING. Panel state, not the toolkit's: a record always has one or the other, but
	 * the UI has to be able to sit on Data Table with nothing picked yet. Seeded from whatever is bound at construction.
	 */
	EQuestPlanSourceKind ShownSourceKind = EQuestPlanSourceKind::Folder;

	/** Backing store for the format combo. Refreshed on open rather than at construction, because a provider can be
	 *  registered after this panel exists and a snapshot would hide it. */
	TArray<TSharedPtr<FString>> FormatOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> FormatCombo;

	void RefreshFormatOptions();

	FText Summary;
	FText Blockers;
	/**
	 * Whether what Blockers holds actually STOPS anything. Refusals and contested keys do; warnings do not, and one
	 * banner rendering both in red says the two mean the same thing when the whole point of the plan model is that
	 * they don't.
	 */
	bool bBlocking = false;
	
	bool  bHasPlan = false;

	/**
	 * Which way the displayed plan points. One plan record per questline, so this is what makes the two Apply buttons
	 * mutually exclusive - and whichever is live states the direction spatially, rather than in a label to be read.
	 */
	EQuestPlanDirection PlanDirection = EQuestPlanDirection::IntoGraph;
	
	bool  bStale   = false;
	
	/**
	 * A table this plan concerns has been edited. Separate from bStale because the FIX differs and so does the
	 * sentence: the questline moving and the data moving are different problems with the same remedy.
	 */
	bool bTableStale = false;
	
	FString LastError;
	/**
	 * Format the FAILED read used. Kept separately because the combo may since have moved, and a console run has its
	 * own format selection entirely - so the panel must report what was tried, not what it currently shows.
	 */
	FString LastFailedFormat;

	TArray<FQuestPlanRowPtr> Rows;
	TSharedPtr<SColumnTableView<FQuestPlanRowPtr>> Table;
};

