// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineOutlinerPanel.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestStep.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SQuestlineOutlinerPanel"

void SQuestlineOutlinerRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
{
    Item = InArgs._Item;
    HighlightText = InArgs._HighlightText;
    OnDoubleClicked = InArgs._OnDoubleClicked;

    STableRow<TSharedPtr<FQuestlineOutlinerItem>>::Construct(
        STableRow<TSharedPtr<FQuestlineOutlinerItem>>::FArguments()
        .Style(FAppStyle::Get(), "TableView.AlternatingRow"),
        InOwnerTable);
}

void SQuestlineOutlinerRow::ConstructChildren(ETableViewMode::Type InOwnerTableMode,
    const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent)
{
    if (!Item.IsValid())
    {
        STableRow<TSharedPtr<FQuestlineOutlinerItem>>::ConstructChildren(InOwnerTableMode, InPadding, InContent);
        return;
    }
    const bool bIsLinkedHeader = Item->ItemType == EOutlinerItemType::LinkedGraph;
    const bool bIsDeep = Item->LinkDepth > 1;

    FSlateColor TextColor;
    FSlateFontInfo Font;

    if (Item->ItemType == EOutlinerItemType::Root)
    {
        TextColor = FSlateColor(FLinearColor(0.8f, 0.5f, 0.1f));
        Font = FCoreStyle::GetDefaultFontStyle("Bold", 9);
    }
    else if (bIsLinkedHeader)
    {
        TextColor = FSlateColor(FLinearColor(0.35f, 0.35f, 0.825f));
        Font = FCoreStyle::GetDefaultFontStyle(bIsDeep ? "BoldItalic" : "Bold", 9);
    }
    else if (Item->LinkDepth > 0)
    {
        TextColor = FSlateColor(FLinearColor(0.18f, 0.18f, 0.18f));
        Font = FCoreStyle::GetDefaultFontStyle(bIsDeep ? "Italic" : "Regular", 9);
    }
    else
    {
        TextColor = FSlateColor::UseForeground();
        Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);
    }
    
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
            .Text(FText::FromString(Item->DisplayName))
            .HighlightText(HighlightText)
            .ColorAndOpacity(TextColor)
            .Font(Font)
        ]
    ];
}

FReply SQuestlineOutlinerRow::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    OnDoubleClicked.ExecuteIfBound();
    return FReply::Handled();
}

void SQuestlineOutlinerPanel::Construct(const FArguments& InArgs, UQuestlineGraph* InGraph)
{
    QuestlineGraph = InGraph;
    OnItemNavigate = InArgs._OnItemNavigate;

    SAssignNew(TreeView, STreeView<TSharedPtr<FQuestlineOutlinerItem>>)
        .TreeItemsSource(&VisibleRoots)
        .OnGenerateRow(this, &SQuestlineOutlinerPanel::GenerateRow)
        .OnGetChildren(this, &SQuestlineOutlinerPanel::GetChildQuestlineItems)
        .OnContextMenuOpening(this, &SQuestlineOutlinerPanel::MakeContextMenu);

    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(2.f, 2.f, 2.f, 2.f))
        [
            SNew(SSearchBox)
                .HintText(LOCTEXT("FilterHint", "Filter by tag or label..."))
                .OnTextChanged(this, &SQuestlineOutlinerPanel::HandleFilterTextChanged)
        ]
        + SVerticalBox::Slot().FillHeight(1.f)
        [
            TreeView.ToSharedRef()
        ]
    ];

    RebuildTree();
}

void SQuestlineOutlinerPanel::Refresh()
{
    RebuildTree();
}

void SQuestlineOutlinerPanel::RebuildTree()
{
    RootItems.Empty();
    if (!QuestlineGraph) return;

    // Technical tag prefix — still sourced from QuestlineID so tag-matching downstream stays stable across renames.
    const FString QuestlineID = QuestlineGraph->GetQuestlineID().IsEmpty() ? QuestlineGraph->GetName() : QuestlineGraph->GetQuestlineID();

    auto RootItem = MakeShared<FQuestlineOutlinerItem>();
    RootItem->Tag = FName(*QuestlineID);
    // Display label — prefer FriendlyName via the canonical accessor, falls back to the asset short name when empty.
    RootItem->DisplayName = QuestlineGraph->GetDisplayName().ToString();
    RootItem->ItemType = EOutlinerItemType::Root;

    const TMap<FName, TObjectPtr<UQuestNodeBase>>& CompiledNodes = QuestlineGraph->GetCompiledNodes();

    TMap<FName, TSharedPtr<FQuestlineOutlinerItem>> ItemMap;

    // Tag prefix this asset owns. Only CompiledNodes entries whose key starts with "<RootTagPrefix>." belong to this
    // questline's content tree; everything else (Util_* utility keys, prereq-rule monitor tags namespaced under
    // SimpleQuest.QuestPrereqRule.*, any future foreign-namespace registrations) must be skipped. Without this filter, foreign
    // ancestors flow into MissingIntermediates and cascade non-zero LinkDepth onto every local item, which strips their
    // styling and routes their double-click through the cross-asset branch with a null SourceGraph (silent navigation no-op).
    const FString RootTagPrefixStr = FString::Printf(TEXT("SimpleQuest.Quest.%s"), *FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(QuestlineID));
    const FName   RootTagPrefix(*RootTagPrefixStr);
    const FString RootChildPrefix = RootTagPrefixStr + TEXT(".");
    auto IsLocalContentTag = [&](FName Key) { return Key.ToString().StartsWith(RootChildPrefix); };

    // Pass 1 — items from compiled nodes; classify by serialized runtime metadata.
    //   bIsLinkedQuestlinePlacement (set by compiler)  → LinkedGraph (blue-bold per original spec)
    //   UQuestStep runtime class                       → Step (reserves the slot; same default styling as Quest for now)
    //   anything else                                  → Quest (default)
    // Both signals serialize with the asset, so the panel classifies correctly on first display after editor load.
    // No recompile required. (Earlier draft used CompiledEditorNodes which is UPROPERTY(Transient) and cleared on load.)
    for (const auto& Pair : CompiledNodes)
    {
        if (!IsLocalContentTag(Pair.Key)) continue;

        auto Item = MakeShared<FQuestlineOutlinerItem>();
        Item->Tag  = Pair.Key;
        Item->Node = Pair.Value;

        if (Pair.Value && Pair.Value->IsLinkedQuestlinePlacement())
            Item->ItemType = EOutlinerItemType::LinkedGraph;
        else if (Pair.Value && Pair.Value->IsA<UQuestStep>())
            Item->ItemType = EOutlinerItemType::Step;
        // else default = EOutlinerItemType::Quest (struct default)

        const FString TagStr = Pair.Key.ToString();
        FString Ignored, LastSegment;
        if (!TagStr.Split(TEXT("."), &Ignored, &LastSegment, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
            LastSegment = TagStr;
        Item->DisplayName = LastSegment;

        ItemMap.Add(Pair.Key, Item);
    }

    // Pass 2 — find missing intermediates (linked graph slots)
    // Any ancestor path that is not itself in CompiledNodes and is not the root prefix
    TSet<FName> MissingIntermediates;
    for (const auto& Pair : CompiledNodes)
    {
        if (!IsLocalContentTag(Pair.Key)) continue;

        FString Cursor = Pair.Key.ToString();
        FString Parent, Last;
        while (Cursor.Split(TEXT("."), &Parent, &Last, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            const FName ParentName(*Parent);
            if (ParentName == RootTagPrefix) break;
            if (!ItemMap.Contains(ParentName))
                MissingIntermediates.Add(ParentName);
            Cursor = Parent;
        }
    }

    // Linked-graph slots = synthesized missing intermediates (rare fallback for cases where the wrapper's own tag
    // isn't in CompiledNodes) UNION real LinkedQuestline wrappers classified during Pass 1. Both kinds anchor the
    // depth-cascade for descendant content items below.
    TSet<FName> LinkedGraphSlots = MissingIntermediates;
    for (const auto& ItemPair : ItemMap)
    {
        if (ItemPair.Value->ItemType == EOutlinerItemType::LinkedGraph)
            LinkedGraphSlots.Add(ItemPair.Key);
    }

    // Pass 3 — compute depth and create synthetic LinkedGraph items
    // Depth = 1 (this slot itself) + number of its ancestors that are also linked-graph slots.
    auto ComputeLinkedDepth = [&](FName Key) -> int32
    {
        int32 Depth = 1;
        FString Cursor = Key.ToString();
        FString Parent, Last;
        while (Cursor.Split(TEXT("."), &Parent, &Last, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            if (LinkedGraphSlots.Contains(FName(*Parent)))
                ++Depth;
            Cursor = Parent;
        }
        return Depth;
    };

    for (const FName& MissingKey : MissingIntermediates)
    {
        auto HeaderItem = MakeShared<FQuestlineOutlinerItem>();
        HeaderItem->Tag       = MissingKey;
        HeaderItem->ItemType  = EOutlinerItemType::LinkedGraph;
        HeaderItem->LinkDepth = ComputeLinkedDepth(MissingKey);

        const FString KeyStr = MissingKey.ToString();
        FString Ignored, LastSeg;
        if (!KeyStr.Split(TEXT("."), &Ignored, &LastSeg, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
            LastSeg = KeyStr;
        HeaderItem->DisplayName = LastSeg;

        ItemMap.Add(MissingKey, HeaderItem);
    }

    // Pass 3.5 — assign LinkDepth to real LinkedQuestline wrapper items already in ItemMap from Pass 1.
    // Synthetic intermediates set their own LinkDepth above; real wrappers need it computed from the unified slot set
    // so that nested-linked wrappers (LinkDepth >= 2) render in BoldItalic per the original deep-linked rule.
    for (auto& ItemPair : ItemMap)
    {
        TSharedPtr<FQuestlineOutlinerItem>& Item = ItemPair.Value;
        if (Item->ItemType != EOutlinerItemType::LinkedGraph) continue;
        if (MissingIntermediates.Contains(ItemPair.Key))    continue;  // synthetic, already done

        Item->LinkDepth = ComputeLinkedDepth(ItemPair.Key);
    }

    // Pass 4 — set LinkDepth on content items from their nearest linked graph ancestor
    for (auto& ItemPair : ItemMap)
    {
        TSharedPtr<FQuestlineOutlinerItem>& Item = ItemPair.Value;
        if (Item->ItemType == EOutlinerItemType::LinkedGraph) continue;

        FString Cursor = Item->Tag.ToString();
        FString Parent, Last;
        while (Cursor.Split(TEXT("."), &Parent, &Last, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            if (LinkedGraphSlots.Contains(FName(*Parent)))
            {
                Item->LinkDepth = ComputeLinkedDepth(FName(*Parent));
                break;
            }
            Cursor = Parent;
        }
    }

    // Pass 5 — parent-attach
    for (const auto& ItemPair : ItemMap)
    {
        const TSharedPtr<FQuestlineOutlinerItem>& Item = ItemPair.Value;
        const FString TagStr = Item->Tag.ToString();

        FString ParentStr, LastSeg;
        if (TagStr.Split(TEXT("."), &ParentStr, &LastSeg, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            if (TSharedPtr<FQuestlineOutlinerItem>* ParentItem = ItemMap.Find(FName(*ParentStr)))
            {
                (*ParentItem)->Children.Add(Item);
                continue;
            }
        }
        RootItem->Children.Add(Item);
    }

    RootItems.Add(RootItem);

    if (TreeView.IsValid())
    {
        TreeView->SetItemExpansion(RootItem, true);
        for (const auto& ItemPair : ItemMap)
        {
            TreeView->SetItemExpansion(ItemPair.Value, true);
        }
    }
    // Pass 6 — annotate SourceGraph for navigation
    // Local items always belong to the current asset
    for (auto& ItemPair : ItemMap)
    {
        if (ItemPair.Value->LinkDepth == 0)
        {
            ItemPair.Value->SourceGraph = QuestlineGraph;
        }
    }
    // Cycle guard — circular linked-questline references (A→B→A) would otherwise recurse unboundedly.
    TSet<FSoftObjectPath> VisitedLinkedAssets;

    // Linked items need their source asset resolved from the graph's linked questline nodes
    TFunction<void(UEdGraph*, const FString&, UQuestlineGraph*)> ResolveLinkedSources =
        [&](UEdGraph* Graph, const FString& TagPrefix, UQuestlineGraph* CurrentAsset)
        {
            if (!Graph) return;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (UQuestlineNode_LinkedQuestline* LinkedNode = Cast<UQuestlineNode_LinkedQuestline>(Node))
                {
                    if (LinkedNode->LinkedGraph.IsNull()) continue;

                    UQuestlineGraph* LinkedAsset = LinkedNode->LinkedGraph.LoadSynchronous();
                    if (!LinkedAsset) continue;

                    const FSoftObjectPath LinkedPath(LinkedAsset);
                    if (VisitedLinkedAssets.Contains(LinkedPath)) continue;

                    const FString LinkedID = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(
                        LinkedAsset->GetQuestlineID().IsEmpty() ? LinkedAsset->GetName() : LinkedAsset->GetQuestlineID());

                    const FString LinkedPrefix    = TagPrefix + TEXT(".") + LinkedID;
                    const FString FullLinkedPrefix = TEXT("SimpleQuest.Quest.") + LinkedPrefix;

                    for (auto& ItemPair : ItemMap)
                    {
                        if (ItemPair.Value->Tag.ToString().StartsWith(FullLinkedPrefix))
                            ItemPair.Value->SourceGraph = LinkedAsset;
                    }

                    if (TSharedPtr<FQuestlineOutlinerItem>* Header = ItemMap.Find(FName(*FullLinkedPrefix)))
                    {
                        (*Header)->ContainingAsset = CurrentAsset;
                    }

                    if (LinkedAsset->QuestlineEdGraph)
                    {
                        VisitedLinkedAssets.Add(LinkedPath);
                        ResolveLinkedSources(LinkedAsset->QuestlineEdGraph, LinkedPrefix, LinkedAsset);
                        VisitedLinkedAssets.Remove(LinkedPath);
                    }
                }
                else if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
                {
                    UEdGraph* InnerGraph = QuestNode->GetInnerGraph();
                    if (!InnerGraph) continue;

                    const FString QuestLabel = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(QuestNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                    if (QuestLabel.IsEmpty()) continue;

                    ResolveLinkedSources(InnerGraph, TagPrefix + TEXT(".") + QuestLabel, CurrentAsset);
                }
            }
        };

    const FString RootPrefix = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(QuestlineID);
    ResolveLinkedSources(QuestlineGraph->QuestlineEdGraph, RootPrefix, QuestlineGraph);

    // Re-derive VisibleRoots / VisibleItemSet against the freshly-rebuilt RootItems and the current FilterText (if any).
    RebuildVisibleTree();
    if (TreeView.IsValid())
    {
        TreeView->RequestTreeRefresh();
    }
}

TSharedRef<ITableRow> SQuestlineOutlinerPanel::GenerateRow(TSharedPtr<FQuestlineOutlinerItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SQuestlineOutlinerRow, OwnerTable)
        .Item(Item)
        .HighlightText(this, &SQuestlineOutlinerPanel::GetFilterText)
        .OnDoubleClicked_Lambda([this, Item]()
        {
            if (OnItemNavigate.IsBound())
                OnItemNavigate.Execute(Item);
        });
}


void SQuestlineOutlinerPanel::GetChildQuestlineItems(TSharedPtr<FQuestlineOutlinerItem> Item, TArray<TSharedPtr<FQuestlineOutlinerItem>>& OutChildren)
{
    if (!Item.IsValid()) return;

    if (FilterText.IsEmpty())
    {
        OutChildren = Item->Children;
        return;
    }

    // Active filter — drop children that aren't in the visible set (themselves not matching and no descendant matching).
    OutChildren.Reset();
    for (const TSharedPtr<FQuestlineOutlinerItem>& Child : Item->Children)
    {
        if (Child.IsValid() && VisibleItemSet.Contains(Child))
        {
            OutChildren.Add(Child);
        }
    }
}

TSharedPtr<SWidget> SQuestlineOutlinerPanel::MakeContextMenu()
{
    if (!TreeView.IsValid()) return nullptr;

    const TArray<TSharedPtr<FQuestlineOutlinerItem>> Selected = TreeView->GetSelectedItems();
    if (Selected.IsEmpty() || !Selected[0].IsValid()) return nullptr;

    FMenuBuilder MenuBuilder(true, nullptr);
    MenuBuilder.BeginSection(NAME_None, LOCTEXT("ClipboardSection", "Clipboard"));
    MenuBuilder.AddMenuEntry(
        LOCTEXT("CopyTagLabel", "Copy Tag"),
        LOCTEXT("CopyTagTooltip", "Copy this row's tag (full runtime tag for Quest / Step / Linked nodes; questline ID for the root row) to the clipboard."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(this, &SQuestlineOutlinerPanel::CopySelectedItemTag)));
    MenuBuilder.EndSection();
    return MenuBuilder.MakeWidget();
}

void SQuestlineOutlinerPanel::CopySelectedItemTag()
{
    if (!TreeView.IsValid()) return;

    const TArray<TSharedPtr<FQuestlineOutlinerItem>> Selected = TreeView->GetSelectedItems();
    if (Selected.IsEmpty() || !Selected[0].IsValid() || Selected[0]->Tag.IsNone()) return;

    FPlatformApplicationMisc::ClipboardCopy(*Selected[0]->Tag.ToString());
}

void SQuestlineOutlinerPanel::HandleFilterTextChanged(const FText& NewText)
{
    const FString NewFilter = NewText.ToString();
    const bool bWasFiltering = !FilterText.IsEmpty();
    const bool bIsFiltering  = !NewFilter.IsEmpty();

    // Save expansion state on the empty -> non-empty transition so we can restore it when the user clears the filter.
    if (bIsFiltering && !bWasFiltering)
    {
        SaveExpansionState();
    }

    FilterText = NewFilter;
    RebuildVisibleTree();

    if (bIsFiltering)
    {
        // Auto-expand every visible item so matches are reachable without manual drill-down. Subsequent filter
        // changes (typing more / less) just re-expand the new visible set; non-matching expansion is dropped
        // because those items are no longer in the visible source array Slate is iterating.
        AutoExpandVisibleItems();
    }
    else if (bWasFiltering)
    {
        RestoreExpansionState();
    }

    if (TreeView.IsValid())
    {
        TreeView->RequestTreeRefresh();
    }
}

void SQuestlineOutlinerPanel::RebuildVisibleTree()
{
    VisibleRoots.Reset();
    VisibleItemSet.Reset();

    if (FilterText.IsEmpty())
    {
        VisibleRoots = RootItems;  // shallow copy of TSharedPtrs — same items, no clone.
        return;
    }

    for (const TSharedPtr<FQuestlineOutlinerItem>& Root : RootItems)
    {
        if (CollectVisible(Root))
        {
            VisibleRoots.Add(Root);
        }
    }
}

bool SQuestlineOutlinerPanel::CollectVisible(TSharedPtr<FQuestlineOutlinerItem> Item)
{
    if (!Item.IsValid()) return false;

    bool bAnyVisible = false;
    if (ItemMatches(*Item))
    {
        VisibleItemSet.Add(Item);
        bAnyVisible = true;
    }

    for (const TSharedPtr<FQuestlineOutlinerItem>& Child : Item->Children)
    {
        if (CollectVisible(Child))
        {
            // Ancestor preservation — any item with a matching descendant stays visible to keep the path readable.
            VisibleItemSet.Add(Item);
            bAnyVisible = true;
        }
    }

    return bAnyVisible;
}

bool SQuestlineOutlinerPanel::ItemMatches(const FQuestlineOutlinerItem& Item) const
{
    // Case-insensitive substring match (FString::Contains defaults to ESearchCase::IgnoreCase). Matching either
    // DisplayName or the full Tag string lets designers find rows by label or by partial gameplay-tag fragment.
    return Item.DisplayName.Contains(FilterText) || Item.Tag.ToString().Contains(FilterText);
}

void SQuestlineOutlinerPanel::SaveExpansionState()
{
    SavedExpansionState.Reset();
    if (!TreeView.IsValid()) return;

    // Capture expanded items by tag (stable identity across recompile) rather than TSharedPtr (rebuilt on Refresh,
    // would leave us holding stale pointers when restoring after a mid-filter compile).
    TSet<TSharedPtr<FQuestlineOutlinerItem>> Expanded;
    TreeView->GetExpandedItems(Expanded);
    for (const TSharedPtr<FQuestlineOutlinerItem>& Item : Expanded)
    {
        if (Item.IsValid() && !Item->Tag.IsNone())
        {
            SavedExpansionState.Add(Item->Tag);
        }
    }
}

void SQuestlineOutlinerPanel::RestoreExpansionState()
{
    if (!TreeView.IsValid()) return;

    TreeView->ClearExpandedItems();
    if (SavedExpansionState.IsEmpty()) return;

    // Walk the current tree once and re-apply expansion to any item whose tag matches a saved entry. Cheap given
    // typical questline sizes: dozens of items at the upper end. Items that no longer exist in the post-compile
    // tree silently fall out (their tags aren't found); items in the rebuilt tree with previously-saved tags get
    // their expansion restored.
    TFunction<void(const TSharedPtr<FQuestlineOutlinerItem>&)> ApplyExpansion =
        [this, &ApplyExpansion](const TSharedPtr<FQuestlineOutlinerItem>& Item)
        {
            if (!Item.IsValid()) return;
            if (SavedExpansionState.Contains(Item->Tag))
            {
                TreeView->SetItemExpansion(Item, true);
            }
            for (const TSharedPtr<FQuestlineOutlinerItem>& Child : Item->Children)
            {
                ApplyExpansion(Child);
            }
        };

    for (const TSharedPtr<FQuestlineOutlinerItem>& Root : RootItems)
    {
        ApplyExpansion(Root);
    }
}

void SQuestlineOutlinerPanel::AutoExpandVisibleItems()
{
    if (!TreeView.IsValid()) return;

    for (const TSharedPtr<FQuestlineOutlinerItem>& Item : VisibleItemSet)
    {
        if (Item.IsValid())
        {
            TreeView->SetItemExpansion(Item, true);
        }
    }
}

#undef LOCTEXT_NAMESPACE
