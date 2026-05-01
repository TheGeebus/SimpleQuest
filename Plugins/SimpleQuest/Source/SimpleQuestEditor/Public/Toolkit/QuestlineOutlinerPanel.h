// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class UQuestlineGraph;
class UQuestNodeBase;

enum class EOutlinerItemType : uint8
{
	Root,         // The questline asset itself — always the tree root
	Quest,        // A UQuestlineNode_Quest in the top-level graph
	Step,         // A UQuestlineNode_Step inside a quest's inner graph
	LinkedGraph,  // A UQuestlineNode_LinkedQuestline — greyed out
};

struct FQuestlineOutlinerItem
{
	FName Tag;
	FString DisplayName;
	UQuestNodeBase* Node = nullptr;
	EOutlinerItemType ItemType = EOutlinerItemType::Quest;
	int32 LinkDepth = 0;										// 0 = local, 1 = first-level linked, 2+ = deeper
	TObjectPtr<UQuestlineGraph> SourceGraph = nullptr;			// asset directly hosting this node; navigation only
	TObjectPtr<UQuestlineGraph> ContainingAsset = nullptr;		// LinkedGraph items only: the asset whose graph holds the UQuestlineNode_LinkedQuestline pointing here
	TArray<TSharedPtr<FQuestlineOutlinerItem>> Children;

	bool IsLinked() const { return ItemType == EOutlinerItemType::LinkedGraph; }
	bool IsDeepLinked() const { return LinkDepth > 1; }
};

class SQuestlineOutlinerRow : public STableRow<TSharedPtr<FQuestlineOutlinerItem>>
{
public:
	SLATE_BEGIN_ARGS(SQuestlineOutlinerRow) {}
	SLATE_ARGUMENT(TSharedPtr<FQuestlineOutlinerItem>, Item)
	SLATE_ATTRIBUTE(FText, HighlightText)
	SLATE_EVENT(FSimpleDelegate, OnDoubleClicked)
SLATE_END_ARGS()
	
void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable);
	virtual void ConstructChildren(ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedPtr<FQuestlineOutlinerItem> Item;
	TAttribute<FText> HighlightText;
	FSimpleDelegate OnDoubleClicked;
	
};

DECLARE_DELEGATE_OneParam(FOnOutlinerItemNavigate, TSharedPtr<FQuestlineOutlinerItem>)

class SQuestlineOutlinerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestlineOutlinerPanel) {}
		SLATE_EVENT(FOnOutlinerItemNavigate, OnItemNavigate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UQuestlineGraph* InGraph);

	/** Rebuilds the tree from the current compiled state of the graph. Call after compilation. */
	void Refresh();

	/** Current filter text — exposed so the row widget can bind STextBlock::HighlightText. */
	FText GetFilterText() const { return FText::FromString(FilterText); }

private:
	void RebuildTree();
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FQuestlineOutlinerItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void GetChildQuestlineItems(TSharedPtr<FQuestlineOutlinerItem> Item, TArray<TSharedPtr<FQuestlineOutlinerItem>>& OutChildren);

	TSharedPtr<SWidget> MakeContextMenu();
	void CopySelectedItemTag();

	void HandleFilterTextChanged(const FText& NewText);
	void RebuildVisibleTree();
	bool CollectVisible(TSharedPtr<FQuestlineOutlinerItem> Item);
	bool ItemMatches(const FQuestlineOutlinerItem& Item) const;
	void SaveExpansionState();
	void RestoreExpansionState();
	void AutoExpandVisibleItems();

	TObjectPtr<UQuestlineGraph> QuestlineGraph;

	/** Source-of-truth tree, rebuilt on Refresh(). */
	TArray<TSharedPtr<FQuestlineOutlinerItem>> RootItems;

	/** Filtered roots that the TreeView actually reads. Equals RootItems when FilterText is empty. */
	TArray<TSharedPtr<FQuestlineOutlinerItem>> VisibleRoots;

	/** Set of items visible under the current filter (matching items + ancestors of matches). Empty when no filter. */
	TSet<TSharedPtr<FQuestlineOutlinerItem>> VisibleItemSet;

	/** Current filter substring, lowercased semantics via FString::Contains case-insensitive default. */
	FString FilterText;
	
	/**
	 * Expansion state captured when the filter went from empty to non-empty; restored on filter clear. Keyed by
	 * Item->Tag (FName) rather than by TSharedPtr so a recompile happening mid-filter doesn't strand the saved
	 * pointers - RestoreExpansionState re-resolves against the current RootItems tree.
	 */
	TSet<FName> SavedExpansionState;

	TSharedPtr<STreeView<TSharedPtr<FQuestlineOutlinerItem>>> TreeView;
	FOnOutlinerItemNavigate OnItemNavigate;		
};
