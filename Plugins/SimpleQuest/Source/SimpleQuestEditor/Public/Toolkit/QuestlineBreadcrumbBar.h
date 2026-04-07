// Copyright 2026, Greg Bussell, All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UEdGraph;
class UEdGraphNode;
class SHorizontalBox;

DECLARE_DELEGATE_OneParam(FOnQuestlineCrumbClicked, UEdGraph*)
DECLARE_DELEGATE_TwoParams(FOnQuestlineDelimiterClicked, UEdGraph*, UEdGraphNode*)

struct FQuestlineBreadcrumb
{
	FText DisplayName;
	UEdGraph* Graph = nullptr;				// crumb click: navigate to this graph
	UEdGraphNode* EntryNode = nullptr;		// delimiter click: select this node in the parent graph; null on root crumb
};

class SQuestlineBreadcrumbBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestlineBreadcrumbBar) {}
		SLATE_EVENT(FOnQuestlineCrumbClicked, OnCrumbClicked)
		SLATE_EVENT(FOnQuestlineDelimiterClicked, OnDelimiterClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetCrumbs(const TArray<FQuestlineBreadcrumb>& InCrumbs);

private:
	void Rebuild();

	TArray<FQuestlineBreadcrumb> Crumbs;
	FOnQuestlineCrumbClicked OnCrumbClicked;
	FOnQuestlineDelimiterClicked OnDelimiterClicked;
	TSharedPtr<SHorizontalBox> Box;
};
