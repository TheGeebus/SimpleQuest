// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SWorldStateFactsPanel.h"

#include "Debug/SimpleCorePIEDebugChannel.h"
#include "SimpleCoreEditor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Input/SSearchBox.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "SWorldStateFactsPanel"

namespace WorldStateFactsPanel_ColumnIds
{
    const FName Tag   = TEXT("Tag");
    const FName Count = TEXT("Count");
}

namespace WorldStateFactsPanel_Style
{
    const FLinearColor DebugActiveBadgeTint = FLinearColor(FColor(250, 200, 60));   // matches graph "DEBUG (PIE)" badge
    const FLinearColor SubduedText          = FLinearColor(0.55f, 0.55f, 0.55f);
}

class SWorldStateFactRow : public SMultiColumnTableRow<TSharedPtr<FWorldStateFactRow>>
{
public:
    SLATE_BEGIN_ARGS(SWorldStateFactRow) {}
        SLATE_ARGUMENT(TSharedPtr<FWorldStateFactRow>, Item)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
    {
        Item = InArgs._Item;
        SMultiColumnTableRow<TSharedPtr<FWorldStateFactRow>>::Construct(FSuperRowType::FArguments(), OwnerTable);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
    {
        if (!Item.IsValid())
        {
            return SNullWidget::NullWidget;
        }

        if (ColumnName == WorldStateFactsPanel_ColumnIds::Tag)
        {
            return SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock)
                        .Text(FText::FromName(Item->Tag.GetTagName()))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ];
        }
        if (ColumnName == WorldStateFactsPanel_ColumnIds::Count)
        {
            return SNew(SBox).Padding(FMargin(6.f, 2.f)).HAlign(HAlign_Right)
                [
                    SNew(STextBlock)
                        .Text(FText::AsNumber(Item->Count))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ];
        }
        return SNullWidget::NullWidget;
    }

private:
    TSharedPtr<FWorldStateFactRow> Item;
};

void SWorldStateFactsPanel::Construct(const FArguments& InArgs)
{
    // Subscribe to PIE-active transitions via the module's channel. A full rebuild fires on start/end; per-tick polling
    // during an active session catches incremental fact churn without needing a per-fact event subscription.
    if (FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel())
    {
        DebugActiveHandle = Channel->OnDebugActiveChanged.AddRaw(this, &SWorldStateFactsPanel::HandleDebugActiveChanged);
    }

    TSharedRef<SHeaderRow> Header = SNew(SHeaderRow)
        + SHeaderRow::Column(WorldStateFactsPanel_ColumnIds::Tag)
            .DefaultLabel(LOCTEXT("ColTag", "Tag"))
            .FillWidth(0.75f)
        + SHeaderRow::Column(WorldStateFactsPanel_ColumnIds::Count)
            .DefaultLabel(LOCTEXT("ColCount", "Count"))
            .FillWidth(0.25f)
            .HAlignHeader(HAlign_Right);

    ListView = SNew(SListView<TSharedPtr<FWorldStateFactRow>>)
        .ListItemsSource(&Rows)
        .OnGenerateRow(this, &SWorldStateFactsPanel::HandleGenerateRow)
        .HeaderRow(Header)
        .SelectionMode(ESelectionMode::None);

    ChildSlot
    [
        SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(4.f))
            [
                SNew(SVerticalBox)
                // Header row — DEBUG (PIE) badge on the left, fact-count status on the right.
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
                    [
                        SNew(STextBlock)
                            .Text(LOCTEXT("HeaderTitle", "WORLD STATE FACTS"))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                            .Text_Lambda([this]() { return GetStatusText(); })
                            .ColorAndOpacity(FSlateColor(WorldStateFactsPanel_Style::SubduedText))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    ]
                ]
                // Filter row — case-insensitive substring match on the tag name. Live filter; no apply button.
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
                [
                    SNew(SSearchBox)
                        .HintText(LOCTEXT("FilterHint", "Filter by tag..."))
                        .OnTextChanged(this, &SWorldStateFactsPanel::HandleFilterTextChanged)
                ]
                // Main body — list when active with rows, centered message otherwise.
                + SVerticalBox::Slot().FillHeight(1.f)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SNew(SBox)
                            .Visibility_Lambda([this]() { return GetListVisibility(); })
                            [ ListView.ToSharedRef() ]
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                            .Text_Lambda([this]() { return GetEmptyMessageText(); })
                            .ColorAndOpacity(FSlateColor(WorldStateFactsPanel_Style::SubduedText))
                            .Visibility_Lambda([this]() { return GetEmptyMessageVisibility(); })
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    ]
                ]
            ]
    ];

    // Populate immediately in case the panel is opened while PIE is already running.
    RebuildRows();
}

SWorldStateFactsPanel::~SWorldStateFactsPanel()
{
    if (DebugActiveHandle.IsValid())
    {
        if (FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel())
        {
            Channel->OnDebugActiveChanged.Remove(DebugActiveHandle);
        }
        DebugActiveHandle.Reset();
    }
}

void SWorldStateFactsPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    // Poll WorldState while the channel is active. Cheap — a TMap size check + per-pair compare against the cached
    // Rows array. If profiling shows cost later, swap to event-driven refresh via FWorldStateFactAddedEvent /
    // FWorldStateFactRemovedEvent on the SignalSubsystem.
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (Channel && Channel->IsActive())
    {
        if (RefreshRowsFromChannel() && ListView.IsValid())
        {
            ListView->RequestListRefresh();
        }
    }
    else if (!AllRows.IsEmpty() || !Rows.IsEmpty())
    {
        AllRows.Reset();
        Rows.Reset();
        if (ListView.IsValid())
        {
            ListView->RequestListRefresh();
        }
    }
}

TSharedRef<ITableRow> SWorldStateFactsPanel::HandleGenerateRow(TSharedPtr<FWorldStateFactRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SWorldStateFactRow, OwnerTable).Item(Item);
}

EVisibility SWorldStateFactsPanel::GetListVisibility() const
{
    return Rows.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SWorldStateFactsPanel::GetEmptyMessageVisibility() const
{
    return Rows.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SWorldStateFactsPanel::GetEmptyMessageText() const
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel || !Channel->IsActive())
    {
        return LOCTEXT("EmptyNotInPIE", "Not in PIE — start Play In Editor to inspect live WorldState facts.");
    }
    if (AllRows.IsEmpty())
    {
        return LOCTEXT("EmptyNoFacts", "PIE active — no facts have been asserted yet.");
    }
    // AllRows non-empty but Rows empty ⇒ filter is excluding everything.
    return FText::Format(LOCTEXT("EmptyFilterMismatch", "No facts match filter '{0}'."), FText::FromString(FilterText));
}

FText SWorldStateFactsPanel::GetStatusText() const
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel || !Channel->IsActive())
    {
        return LOCTEXT("StatusIdle", "idle");
    }
    if (AllRows.Num() != Rows.Num())
    {
        return FText::Format(LOCTEXT("StatusActiveFiltered", "DEBUG (PIE) — {0} fact(s), {1} shown"),
            FText::AsNumber(AllRows.Num()), FText::AsNumber(Rows.Num()));
    }
    return FText::Format(LOCTEXT("StatusActive", "DEBUG (PIE) — {0} fact(s)"), FText::AsNumber(AllRows.Num()));
}

void SWorldStateFactsPanel::HandleDebugActiveChanged()
{
    RebuildRows();
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

void SWorldStateFactsPanel::RebuildRows()
{
    AllRows.Reset();
    Rows.Reset();
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel || !Channel->IsActive())
    {
        return;
    }
    RefreshRowsFromChannel();
}

void SWorldStateFactsPanel::HandleFilterTextChanged(const FText& NewText)
{
    FilterText = NewText.ToString();
    ApplyFilter();
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

void SWorldStateFactsPanel::ApplyFilter()
{
    Rows.Reset();
    if (FilterText.IsEmpty())
    {
        Rows.Append(AllRows);
        return;
    }
    // Case-insensitive substring match against the full tag name. FString::Contains defaults to IgnoreCase.
    for (const TSharedPtr<FWorldStateFactRow>& Row : AllRows)
    {
        if (Row.IsValid() && Row->Tag.GetTagName().ToString().Contains(FilterText))
        {
            Rows.Add(Row);
        }
    }
}

bool SWorldStateFactsPanel::RefreshRowsFromChannel()
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel || !Channel->IsActive())
    {
        return false;
    }

    UWorldStateSubsystem* WorldState = Channel->GetWorldState();
    if (!WorldState)
    {
        return false;
    }

    const TMap<FGameplayTag, int32>& Facts = WorldState->GetAllFacts();

    // Cheap change detection on the unfiltered set — short-circuit when size matches AND every row still points to
    // a live fact with the same count. If WorldState is unchanged since last tick, skip the rebuild and filter pass.
    if (Facts.Num() == AllRows.Num())
    {
        bool bAllMatch = true;
        for (const TSharedPtr<FWorldStateFactRow>& Row : AllRows)
        {
            if (!Row.IsValid()) { bAllMatch = false; break; }
            const int32* Current = Facts.Find(Row->Tag);
            if (!Current || *Current != Row->Count) { bAllMatch = false; break; }
        }
        if (bAllMatch)
        {
            return false;
        }
    }

    AllRows.Reset(Facts.Num());
    for (const TPair<FGameplayTag, int32>& Pair : Facts)
    {
        AllRows.Add(MakeShared<FWorldStateFactRow>(Pair.Key, Pair.Value));
    }
    AllRows.Sort([](const TSharedPtr<FWorldStateFactRow>& A, const TSharedPtr<FWorldStateFactRow>& B)
    {
        if (!A.IsValid() || !B.IsValid()) return false;
        return A->Tag.GetTagName().LexicalLess(B->Tag.GetTagName());
    });
    ApplyFilter();
    return true;
}

#undef LOCTEXT_NAMESPACE
