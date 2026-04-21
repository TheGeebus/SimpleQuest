// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineOutlinerPanel.h"

#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Text/STextBlock.h"



void SQuestlineOutlinerRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
{
    Item = InArgs._Item;
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
        .TreeItemsSource(&RootItems)
        .OnGenerateRow(this, &SQuestlineOutlinerPanel::GenerateRow)
        .OnGetChildren(this, &SQuestlineOutlinerPanel::GetChildQuestlineItems);

    ChildSlot[ TreeView.ToSharedRef() ];

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

    const FString QuestlineID = QuestlineGraph->GetQuestlineID().IsEmpty() ? QuestlineGraph->GetName() : QuestlineGraph->GetQuestlineID();

    auto RootItem = MakeShared<FQuestlineOutlinerItem>();
    RootItem->Tag = FName(*QuestlineID);
    RootItem->DisplayName = QuestlineID;
    RootItem->ItemType = EOutlinerItemType::Root;

    const TMap<FName, TObjectPtr<UQuestNodeBase>>& CompiledNodes = QuestlineGraph->GetCompiledNodes();

    TMap<FName, TSharedPtr<FQuestlineOutlinerItem>> ItemMap;

    // Pass 1 — items from compiled nodes
    for (const auto& Pair : CompiledNodes)
    {
        if (Pair.Key.ToString().StartsWith(TEXT("Util_"))) continue;
        
        auto Item = MakeShared<FQuestlineOutlinerItem>();
        Item->Tag  = Pair.Key;
        Item->Node = Pair.Value;

        const FString TagStr = Pair.Key.ToString();
        FString Ignored, LastSegment;
        if (!TagStr.Split(TEXT("."), &Ignored, &LastSegment, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
            LastSegment = TagStr;
        Item->DisplayName = LastSegment;

        ItemMap.Add(Pair.Key, Item);
    }

    // Pass 2 — find missing intermediates (linked graph slots)
    // Any ancestor path that is not itself in CompiledNodes and is not the root prefix
    const FName RootTagPrefix = FName(*FString::Printf(TEXT("Quest.%s"), *FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(QuestlineID)));

    TSet<FName> MissingIntermediates;
    for (const auto& Pair : CompiledNodes)
    {
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

    // Pass 3 — compute depth and create synthetic LinkedGraph items
    // Depth of a missing intermediate = number of its ancestors that are also missing intermediates + 1
    auto ComputeLinkedDepth = [&](FName Key) -> int32
    {
        int32 Depth = 1;
        FString Cursor = Key.ToString();
        FString Parent, Last;
        while (Cursor.Split(TEXT("."), &Parent, &Last, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            if (MissingIntermediates.Contains(FName(*Parent)))
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

    // Pass 4 — set LinkDepth on content items from their nearest linked graph ancestor
    for (auto& ItemPair : ItemMap)
    {
        TSharedPtr<FQuestlineOutlinerItem>& Item = ItemPair.Value;
        if (Item->ItemType == EOutlinerItemType::LinkedGraph) continue;

        FString Cursor = Item->Tag.ToString();
        FString Parent, Last;
        while (Cursor.Split(TEXT("."), &Parent, &Last, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            if (MissingIntermediates.Contains(FName(*Parent)))
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
        TreeView->RequestTreeRefresh();
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
                    const FString FullLinkedPrefix = TEXT("Quest.") + LinkedPrefix;

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

}

TSharedRef<ITableRow> SQuestlineOutlinerPanel::GenerateRow(TSharedPtr<FQuestlineOutlinerItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SQuestlineOutlinerRow, OwnerTable)
        .Item(Item)
        .OnDoubleClicked_Lambda([this, Item]()
        {
            if (OnItemNavigate.IsBound())
                OnItemNavigate.Execute(Item);
        });
}


void SQuestlineOutlinerPanel::GetChildQuestlineItems(TSharedPtr<FQuestlineOutlinerItem> Item, TArray<TSharedPtr<FQuestlineOutlinerItem>>& OutChildren)
{
    OutChildren = Item->Children;
}

