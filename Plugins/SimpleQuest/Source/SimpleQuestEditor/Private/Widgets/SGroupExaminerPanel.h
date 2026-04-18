// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utilities/GroupExaminerTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class ITableRow;
class SBox;
class STableViewBase;
class UEdGraphNode;
class UQuestlineGraph;

enum class EExaminerItemKind : uint8
{
	Section,    // "Setters" or "Getters" root row
	Endpoint,   // a setter or getter node
	Reference,  // a source or destination content node
};

/**
 * Tree node for the Group Examiner. bIsSetterFamily/bIsGetterFamily drive per-row coloring: Section and Endpoint rows
 * inherit their family's color (bold on Section, regular on Endpoint). Reference rows leave both false and render with
 * default foreground text.
 */
struct FExaminerTreeItem
{
	EExaminerItemKind Kind = EExaminerItemKind::Section;
	FText DisplayLabel;
	TWeakObjectPtr<UEdGraphNode> Node;
	TWeakObjectPtr<UQuestlineGraph> Asset;
	TArray<TSharedPtr<FExaminerTreeItem>> Children;

	bool bIsSetterFamily = false;
	bool bIsGetterFamily = false;
};

/**
 * Custom table row with alternating-background style, wire-drawing expander arrow, and per-kind text styling. Mirrors
 * SQuestlineOutlinerRow's pattern so the examiner visually matches the Outliner panel. ConstructChildren overrides the
 * default row content to inject the SExpanderArrow + STextBlock layout with the chosen color/font.
 */
class SGroupExaminerRow : public STableRow<TSharedPtr<FExaminerTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SGroupExaminerRow) {}
	SLATE_ARGUMENT(TSharedPtr<FExaminerTreeItem>, Item)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable);

protected:
	virtual void ConstructChildren(ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent) override;

private:
	TSharedPtr<FExaminerTreeItem> Item;
};

/**
 * Standalone Slate panel for the Group Examiner feature — pins a group tag and renders its full topology (setters, getters,
 * and their wired sources/destinations) as a tree. Pinned via FQuestlineGraphEditor::PinGroupExaminer, which is invoked by
 * the "Examine Group Connections" context-menu action on activation group nodes. Designer navigates the graph freely without
 * losing the examiner's pin; an explicit Refresh button re-runs topology gathering for the pinned tag.
 *
 * Tree layout:
 *   Setters (N) / Getters (N)           — section roots; expandable; no navigation target
 *     Endpoint (setter or getter node)  — labeled "<Node> in <Asset> (M)"; double-click navigates to node
 *       Reference                       — source or destination; labeled "<PinLabel> on <Node>"; double-click navigates
 *
 * Tier-2 hover highlight on graph nodes is planned as a follow-up wave (Bv2-5) — it requires a custom SGraphPanel paint
 * pass and is cleanly scoped separate from the tree-panel core.
 */
class SGroupExaminerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGroupExaminerPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Pin the given group tag for examination. InPinnedEndpointNode is the setter/getter that initiated the pin (from the
	 * context menu); used to mark "(pinned)" on its row for orientation. Re-runs topology gathering and rebuilds the tree.
	 */
	void PinGroup(FGameplayTag InGroupTag, UEdGraphNode* InPinnedEndpointNode);

	/** Re-runs topology gathering against the currently-pinned tag. Invoked by the Refresh button and externally if needed. */
	void Refresh();

	/**
	 * Finds the tree row whose Node matches the given editor node, expands ancestors, selects the row, and scrolls it into
	 * view. No-op if no row matches (node not in the current topology). Called after cross-editor navigation so the target
	 * panel's highlighted row reflects the newly-arrived-at node rather than lingering on the last-clicked row.
	 */
	void SelectRowForNode(UEdGraphNode* Node);

private:

	using FTreeItemPtr = TSharedPtr<FExaminerTreeItem>;

	// ---- Pinned state ----
	FGameplayTag PinnedGroupTag;
	TWeakObjectPtr<UEdGraphNode> PinnedEndpointNode;
	FGroupExaminerTopology Topology;
	TArray<FTreeItemPtr> RootItems;

	// ---- Widgets ----
	TSharedPtr<SBox> ContentContainer;
	TSharedPtr<STreeView<FTreeItemPtr>> TreeView;

	// ---- Build ----
	void RebuildContent();
	void RebuildTree();
	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildTreeContent();
	TSharedRef<SWidget> BuildEmptyState();

	// ---- Tree callbacks ----
	TSharedRef<ITableRow> OnGenerateRow(FTreeItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(FTreeItemPtr Item, TArray<FTreeItemPtr>& OutChildren);
	void OnItemDoubleClicked(FTreeItemPtr Item);

	// ---- Actions ----
	FReply OnRefreshClicked();
	EVisibility GetRefreshButtonVisibility() const;
	FText GetHeaderTagText() const;
};

