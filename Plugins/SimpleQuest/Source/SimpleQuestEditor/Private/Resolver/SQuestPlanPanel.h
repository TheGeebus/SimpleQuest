// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SColumnTableView.h"
#include "Widgets/SCompoundWidget.h"

struct FQuestInPlacePlan;

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

	FString TargetAssetPath;
	FDelegateHandle PublishHandle;

	FText Summary;
	FText Blockers;
	bool  bHasPlan = false;

	TArray<FQuestPlanRowPtr> Rows;
	TSharedPtr<SColumnTableView<FQuestPlanRowPtr>> Table;
};

