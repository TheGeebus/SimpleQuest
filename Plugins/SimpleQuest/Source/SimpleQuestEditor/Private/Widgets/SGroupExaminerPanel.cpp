// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SGroupExaminerPanel.h"

#include "Utilities/SimpleQuestEditorUtils.h"
#include "Quests/QuestlineGraph.h"

#include "EdGraph/EdGraphNode.h"
#include "Styling/AppStyle.h"
#include "Toolkit/QuestlineGraphEditor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

// Helpers
namespace
{
	FText BuildEndpointLabel(const FGroupExaminerEndpoint& Endpoint)
	{
		UEdGraphNode* Node = Endpoint.Node.Get();
		UQuestlineGraph* Asset = Endpoint.Asset.Get();
		if (!Node || !Asset)
		{
			return LOCTEXT("EndpointMissing", "(unresolved endpoint)");
		}
		return FText::Format(
			LOCTEXT("EndpointLabelFmt", "{0} in {1} ({2})"),
			Node->GetNodeTitle(ENodeTitleType::ListView),
			FText::FromString(Asset->GetName()),
			FText::AsNumber(Endpoint.References.Num())
		);
	}

	FText BuildPinnedEndpointLabel(const FGroupExaminerEndpoint& Endpoint)
	{
		return FText::Format(
			LOCTEXT("EndpointPinnedFmt", "{0}  (pinned)"),
			BuildEndpointLabel(Endpoint)
		);
	}

	FText BuildReferenceLabel(const FGroupExaminerReference& Reference)
	{
		UEdGraphNode* Node = Reference.Node.Get();
		if (!Node)
		{
			return LOCTEXT("RefMissing", "(unresolved reference)");
		}
		return FText::Format(
			LOCTEXT("ReferenceLabelFmt", "{0} on {1}"),
			FText::FromString(Reference.PinLabel),
			Node->GetNodeTitle(ENodeTitleType::ListView)
		);
	}
	
	/**
	 * Collects the editor nodes to hover-highlight for a given row. Endpoint and Reference rows highlight their own Node;
	 * Section rows highlight all endpoint nodes in the section (not their references) — gives the designer an at-a-glance
	 * view of the breadth of the group across the project. Section rows of sections with no endpoints return empty.
	 */
	void GatherHoverTargets(const TSharedPtr<FExaminerTreeItem>& Item, TArray<UEdGraphNode*>& OutNodes)
	{
		if (!Item.IsValid()) return;

		if (Item->Kind == EExaminerItemKind::Section)
		{
			for (const TSharedPtr<FExaminerTreeItem>& Child : Item->Children)
			{
				if (Child.IsValid() && Child->Kind == EExaminerItemKind::Endpoint)
				{
					if (UEdGraphNode* Node = Child->Node.Get()) OutNodes.Add(Node);
				}
			}
			return;
		}

		if (UEdGraphNode* Node = Item->Node.Get()) OutNodes.Add(Node);
	}

	/** Groups nodes by the editor currently hosting each, so cross-editor highlight can dispatch per-editor. */
	void GroupNodesByEditor(const TArray<UEdGraphNode*>& Nodes, TMap<FQuestlineGraphEditor*, TArray<UEdGraphNode*>>& OutByEditor)
	{
		for (UEdGraphNode* Node : Nodes)
		{
			if (FQuestlineGraphEditor* Editor = FSimpleQuestEditorUtilities::GetEditorForNode(Node))
			{
				OutByEditor.FindOrAdd(Editor).Add(Node);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// SGroupExaminerRow — custom alternating-row widget with per-kind styling
// ---------------------------------------------------------------------------

void SGroupExaminerRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
{
	Item = InArgs._Item;

	STableRow<TSharedPtr<FExaminerTreeItem>>::Construct(
		STableRow<TSharedPtr<FExaminerTreeItem>>::FArguments()
			.Style(FAppStyle::Get(), "TableView.AlternatingRow"),
		InOwnerTable);
}

void SGroupExaminerRow::ConstructChildren(ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent)
{
	if (!Item.IsValid())
	{
		STableRow<TSharedPtr<FExaminerTreeItem>>::ConstructChildren(InOwnerTableMode, InPadding, InContent);
		return;
	}

	/**
	 * Category family colors. Setter family renders amber/gold (warm — "publish" semantic). Getter family renders sky
	 * blue (cool — "receive" semantic). Reference rows fall through to default foreground, visually nested under their
	 * colored endpoint via SExpanderArrow's indent + wire rendering.
	 */
	static const FLinearColor SetterColor =  SQ_ED_EXAMINER_GROUP_SETTER;
	static const FLinearColor GetterColor = SQ_ED_EXAMINER_GROUP_GETTER;

	FSlateColor TextColor = FSlateColor::UseForeground();
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);

	if (Item->Kind == EExaminerItemKind::Section)
	{
		TextColor = FSlateColor(Item->bIsSetterFamily ? SetterColor : GetterColor);
		Font = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	}
	else if (Item->Kind == EExaminerItemKind::Endpoint)
	{
		TextColor = FSlateColor(Item->bIsSetterFamily ? SetterColor : GetterColor);
		Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	}
	// EExaminerItemKind::Reference uses defaults above.

	this->ChildSlot
	.Padding(InPadding)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
					.ShouldDrawWires(true)
			]
		+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.f, 1.f)
			[
				SNew(STextBlock)
					.Text(Item->DisplayLabel)
					.ColorAndOpacity(TextColor)
					.Font(Font)
			]
	];
}

void SGroupExaminerRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	STableRow<TSharedPtr<FExaminerTreeItem>>::OnMouseEnter(MyGeometry, MouseEvent);

	TArray<UEdGraphNode*> Targets;
	GatherHoverTargets(Item, Targets);
	if (Targets.IsEmpty()) return;

	TMap<FQuestlineGraphEditor*, TArray<UEdGraphNode*>> ByEditor;
	GroupNodesByEditor(Targets, ByEditor);
	for (const auto& Pair : ByEditor)
	{
		Pair.Key->HighlightNodesInViewport(Pair.Value);
	}
}

void SGroupExaminerRow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	STableRow<TSharedPtr<FExaminerTreeItem>>::OnMouseLeave(MouseEvent);

	/**
	 * Clear by re-resolving which editors we set highlights on. Re-enumeration beats state caching because the row's Item
	 * stays stable during hover, and editor lookup is O(1). Clearing the whole editor's highlight set (rather than only
	 * our nodes) is intentional — a hover-leave should cleanly reset, and any other simultaneously-active hover will
	 * re-set its own targets on its next OnMouseEnter.
	 */
	TArray<UEdGraphNode*> Targets;
	GatherHoverTargets(Item, Targets);
	if (Targets.IsEmpty()) return;

	TSet<FQuestlineGraphEditor*> Editors;
	for (UEdGraphNode* Node : Targets)
	{
		if (FQuestlineGraphEditor* Editor = FSimpleQuestEditorUtilities::GetEditorForNode(Node))
		{
			Editors.Add(Editor);
		}
	}
	for (FQuestlineGraphEditor* Editor : Editors)
	{
		Editor->ClearNodeHighlight();
	}
}

void SGroupExaminerPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(6, 6, 6, 4)
			[
				BuildHeader()
			]
		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator).Thickness(1.0f)
			]
		+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(ContentContainer, SBox)
				[
					BuildEmptyState()
				]
			]
	];
}

void SGroupExaminerPanel::PinGroup(FGameplayTag InGroupTag, UEdGraphNode* InPinnedEndpointNode)
{
	/**
	 * Idempotency guard — same tag + same endpoint in means we're already showing the right topology, so skip the rebuild
	 * flicker. Matters when cross-editor navigation roundtrips back through the same editor, or when the context-menu action
	 * fires on an already-pinned node.
	 */
	if (InGroupTag == PinnedGroupTag && InPinnedEndpointNode == PinnedEndpointNode.Get())
	{
		return;
	}
	
	PinnedGroupTag = InGroupTag;
	PinnedEndpointNode = InPinnedEndpointNode;
	Refresh();
}

void SGroupExaminerPanel::Refresh()
{
	if (!PinnedGroupTag.IsValid())
	{
		Topology = FGroupExaminerTopology();
		RootItems.Empty();
		RebuildContent();
		return;
	}

	FSimpleQuestEditorUtilities::CollectActivationGroupTopology(PinnedGroupTag, Topology);
	RebuildTree();
	RebuildContent();
}

void SGroupExaminerPanel::SelectRowForNode(UEdGraphNode* Node)
{
	if (!Node || !TreeView.IsValid()) return;

	/**
	 * Walk the tree capturing the path from root to a matching item so we can expand each ancestor before scrolling the
	 * target into view. Returns early if no item carries the given editor node.
	 */
	TArray<FTreeItemPtr> Path;
	TFunction<bool(FTreeItemPtr)> Search = [Node, &Path, &Search](FTreeItemPtr Item) -> bool
	{
		if (!Item.IsValid()) return false;
		Path.Push(Item);
		if (Item->Node.Get() == Node) return true;
		for (const FTreeItemPtr& Child : Item->Children)
		{
			if (Search(Child)) return true;
		}
		Path.Pop();
		return false;
	};

	bool bFound = false;
	for (const FTreeItemPtr& Root : RootItems)
	{
		if (Search(Root)) { bFound = true; break; }
	}
	if (!bFound) return;

	for (int32 i = 0; i < Path.Num() - 1; ++i)
	{
		TreeView->SetItemExpansion(Path[i], true);
	}
	FTreeItemPtr Target = Path.Last();
	TreeView->ClearSelection();
	TreeView->SetItemSelection(Target, true);
	TreeView->RequestScrollIntoView(Target);
}

void SGroupExaminerPanel::RebuildContent()
{
	if (!ContentContainer.IsValid()) return;

	if (PinnedGroupTag.IsValid())
	{
		ContentContainer->SetContent(BuildTreeContent());
	}
	else
	{
		ContentContainer->SetContent(BuildEmptyState());
	}
}

void SGroupExaminerPanel::RebuildTree()
{
	RootItems.Empty();

	// ---- Setters section ----
	FTreeItemPtr SettersRoot = MakeShared<FExaminerTreeItem>();
	SettersRoot->Kind = EExaminerItemKind::Section;
	SettersRoot->bIsSetterFamily = true;
	SettersRoot->DisplayLabel = FText::Format(LOCTEXT("SettersSectionFmt", "Setters ({0})"), FText::AsNumber(Topology.Setters.Num()));
	for (const FGroupExaminerEndpoint& Endpoint : Topology.Setters)
	{
		FTreeItemPtr EndpointItem = MakeShared<FExaminerTreeItem>();
		EndpointItem->Kind = EExaminerItemKind::Endpoint;
		EndpointItem->bIsSetterFamily = true;
		EndpointItem->Node = Endpoint.Node;
		EndpointItem->Asset = Endpoint.Asset;
		const bool bIsPinned = (Endpoint.Node.Get() == PinnedEndpointNode.Get() && PinnedEndpointNode.IsValid());
		EndpointItem->DisplayLabel = bIsPinned ? BuildPinnedEndpointLabel(Endpoint) : BuildEndpointLabel(Endpoint);

		for (const FGroupExaminerReference& Ref : Endpoint.References)
		{
			FTreeItemPtr RefItem = MakeShared<FExaminerTreeItem>();
			RefItem->Kind = EExaminerItemKind::Reference;
			RefItem->Node = Ref.Node;
			RefItem->Asset = Ref.Asset;
			RefItem->DisplayLabel = BuildReferenceLabel(Ref);
			EndpointItem->Children.Add(RefItem);
		}
		SettersRoot->Children.Add(EndpointItem);
	}
	RootItems.Add(SettersRoot);

	// ---- Getters section ----
	FTreeItemPtr GettersRoot = MakeShared<FExaminerTreeItem>();
	GettersRoot->Kind = EExaminerItemKind::Section;
	GettersRoot->bIsGetterFamily = true;
	GettersRoot->DisplayLabel = FText::Format(LOCTEXT("GettersSectionFmt", "Getters ({0})"), FText::AsNumber(Topology.Getters.Num()));
	for (const FGroupExaminerEndpoint& Endpoint : Topology.Getters)
	{
		FTreeItemPtr EndpointItem = MakeShared<FExaminerTreeItem>();
		EndpointItem->Kind = EExaminerItemKind::Endpoint;
		EndpointItem->bIsGetterFamily = true;
		EndpointItem->Node = Endpoint.Node;
		EndpointItem->Asset = Endpoint.Asset;
		const bool bIsPinned = (Endpoint.Node.Get() == PinnedEndpointNode.Get() && PinnedEndpointNode.IsValid());
		EndpointItem->DisplayLabel = bIsPinned ? BuildPinnedEndpointLabel(Endpoint) : BuildEndpointLabel(Endpoint);

		for (const FGroupExaminerReference& Ref : Endpoint.References)
		{
			FTreeItemPtr RefItem = MakeShared<FExaminerTreeItem>();
			RefItem->Kind = EExaminerItemKind::Reference;
			RefItem->Node = Ref.Node;
			RefItem->Asset = Ref.Asset;
			RefItem->DisplayLabel = BuildReferenceLabel(Ref);
			EndpointItem->Children.Add(RefItem);
		}
		GettersRoot->Children.Add(EndpointItem);
	}
	RootItems.Add(GettersRoot);

	// ---- Refresh tree view if it exists ----
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();

		// Auto-expand sections and all endpoints so the topology is visible by default.
		for (const FTreeItemPtr& Root : RootItems)
		{
			TreeView->SetItemExpansion(Root, true);
			for (const FTreeItemPtr& Endpoint : Root->Children)
			{
				TreeView->SetItemExpansion(Endpoint, true);
			}
		}
	}
}

TSharedRef<SWidget> SGroupExaminerPanel::BuildHeader()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
					.Text_Raw(this, &SGroupExaminerPanel::GetHeaderTagText)
					.Font(FAppStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
			]
		+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
					.Visibility(this, &SGroupExaminerPanel::GetRefreshButtonVisibility)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("RefreshTooltip", "Re-scan the project for setters and getters matching this group tag."))
					.OnClicked(this, &SGroupExaminerPanel::OnRefreshClicked)
					.ContentPadding(FMargin(4, 2))
					[
						SNew(STextBlock)
							.Text(LOCTEXT("RefreshLabel", "Refresh"))
					]
			];
}

TSharedRef<SWidget> SGroupExaminerPanel::BuildTreeContent()
{
	SAssignNew(TreeView, STreeView<FTreeItemPtr>)
		.TreeItemsSource(&RootItems)
		.OnGenerateRow(this, &SGroupExaminerPanel::OnGenerateRow)
		.OnGetChildren(this, &SGroupExaminerPanel::OnGetChildren)
		.OnMouseButtonDoubleClick(this, &SGroupExaminerPanel::OnItemDoubleClicked)
		.SelectionMode(ESelectionMode::Single);

	// Initial expansion — sections and endpoints expanded by default.
	for (const FTreeItemPtr& Root : RootItems)
	{
		TreeView->SetItemExpansion(Root, true);
		for (const FTreeItemPtr& Endpoint : Root->Children)
		{
			TreeView->SetItemExpansion(Endpoint, true);
		}
	}

	return SNew(SScrollBox)
		+ SScrollBox::Slot()
			[
				TreeView.ToSharedRef()
			];
}

TSharedRef<SWidget> SGroupExaminerPanel::BuildEmptyState()
{
	return SNew(SBox)
		.Padding(20.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
				.Text(LOCTEXT("EmptyStateText",
					"Right-click an activation group setter or getter in the graph and choose 'Examine Group Connections' to pin its topology here."))
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

TSharedRef<ITableRow> SGroupExaminerPanel::OnGenerateRow(FTreeItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SGroupExaminerRow, OwnerTable)
		.Item(Item);
}

void SGroupExaminerPanel::OnGetChildren(FTreeItemPtr Item, TArray<FTreeItemPtr>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren.Append(Item->Children);
	}
}

void SGroupExaminerPanel::OnItemDoubleClicked(FTreeItemPtr Item)
{
	if (!Item.IsValid()) return;
	if (Item->Kind == EExaminerItemKind::Section) return;

	UEdGraphNode* TargetNode = Item->Node.Get();
	if (!TargetNode) return;

	FSimpleQuestEditorUtilities::NavigateToEdGraphNode(TargetNode);

	/**
	 * Cross-editor pin continuity — propagate the pinned group to whichever editor now hosts the target. Same-editor
	 * navigation roundtrips and PinGroup's idempotency guard no-ops; cross-asset navigation sets up a fresh target panel
	 * with the same pin so the designer's context carries across asset boundaries.
	 */
	if (PinnedGroupTag.IsValid())
	{
		if (FQuestlineGraphEditor* TargetEditor = FSimpleQuestEditorUtilities::GetEditorForNode(TargetNode))
		{
			TargetEditor->PinGroupExaminer(PinnedGroupTag, PinnedEndpointNode.Get(), TargetNode);
		}
	}
}

FReply SGroupExaminerPanel::OnRefreshClicked()
{
	Refresh();
	return FReply::Handled();
}

EVisibility SGroupExaminerPanel::GetRefreshButtonVisibility() const
{
	return PinnedGroupTag.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SGroupExaminerPanel::GetHeaderTagText() const
{
	if (!PinnedGroupTag.IsValid())
	{
		return LOCTEXT("HeaderNoGroup", "Group Examiner");
	}
	return FText::Format(LOCTEXT("HeaderGroupFmt", "Group Examiner — {0}"), FText::FromString(PinnedGroupTag.ToString()));
}

#undef LOCTEXT_NAMESPACE