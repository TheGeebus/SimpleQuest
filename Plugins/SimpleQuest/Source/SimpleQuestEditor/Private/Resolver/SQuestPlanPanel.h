// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SColumnTableView.h"
#include "Widgets/SCompoundWidget.h"

struct FQuestInPlacePlan;
class UQuestlineGraph;

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

		/** Raised when the panel's own Rebuild button is pressed. The panel does not import; it asks its owner to. */
		SLATE_EVENT(FSimpleDelegate, OnRebuildRequested)

		/** Raised by Apply. Same reason - the toolkit owns the transaction and the source, the panel owns the display. */
		SLATE_EVENT(FSimpleDelegate, OnApplyRequested)
		
		/** Raised when the user asks to pick a source folder. The panel displays; the toolkit browses and imports. */
		SLATE_EVENT(FSimpleDelegate, OnChooseSourceRequested)

		/** What Rebuild would re-read, so the button is not a promise about an unnamed folder. */
		SLATE_ATTRIBUTE(FText, SourceLabel)

		/** Whether Apply is currently permitted. Bound rather than pushed, so it re-evaluates as plans come and go. */
		SLATE_ATTRIBUTE(bool, CanApply)
		
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SQuestPlanPanel() override;

private:
	void HandlePlanPublished(const FString& InAssetPath, const FQuestInPlacePlan& Plan);
	void RebuildRows(const FQuestInPlacePlan& Plan);
	TArray<FTableColumnDef<FQuestPlanRowPtr>> MakeColumns() const;

	FText GetSummaryText() const;
	FText GetBlockersText() const;
	EVisibility GetBlockersVisibility() const;
	EVisibility GetStaleVisibility() const;
	void HandleObjectModified(UObject* Modified);

	FString TargetAssetPath;
	TWeakObjectPtr<UQuestlineGraph> Questline;
	FDelegateHandle PublishHandle;
	FDelegateHandle ModifiedHandle;

	FSimpleDelegate OnRebuildRequested;
	FSimpleDelegate OnApplyRequested;
	TAttribute<bool> CanApply;

	FSimpleDelegate OnChooseSourceRequested;
	TAttribute<FText> SourceLabel;

	FText Summary;
	FText Blockers;
	bool  bHasPlan = false;
	bool  bStale   = false;

	TArray<FQuestPlanRowPtr> Rows;
	TSharedPtr<SColumnTableView<FQuestPlanRowPtr>> Table;
};

