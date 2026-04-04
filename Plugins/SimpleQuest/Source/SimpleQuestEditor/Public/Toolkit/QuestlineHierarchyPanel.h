// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class UQuestlineGraph;
class UQuestNodeBase;

struct FQuestlineHierarchyItem
{
	FGameplayTag Tag;
	FString DisplayName;
	UQuestNodeBase* Node = nullptr;
	TArray<TSharedPtr<FQuestlineHierarchyItem>> Children;
};

class SQuestlineHierarchyPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestlineHierarchyPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UQuestlineGraph* InGraph);

	/** Rebuilds the tree from the current compiled state of the graph. Call after compilation. */
	void Refresh();

private:
	void RebuildTree();
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FQuestlineHierarchyItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void GetChildren(TSharedPtr<FQuestlineHierarchyItem> Item, TArray<TSharedPtr<FQuestlineHierarchyItem>>& OutChildren);

	TObjectPtr<UQuestlineGraph> QuestlineGraph;
	TArray<TSharedPtr<FQuestlineHierarchyItem>> RootItems;
	TSharedPtr<STreeView<TSharedPtr<FQuestlineHierarchyItem>>> TreeView;
};
