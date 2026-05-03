// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SQuestStateView.h"

#include "Debug/QuestPIEDebugChannel.h"
#include "SimpleQuestEditor.h"
#include "SimpleQuestLog.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestEntryRecord.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SimpleCoreEditorWidgetUtils.h"

#define LOCTEXT_NAMESPACE "SQuestStateView"

DECLARE_DELEGATE_OneParam(FOnQuestStateTagRightClicked, const FGameplayTag&);

namespace QuestStateView_Resolutions_ColumnIDs
{
    const FName Quest   = TEXT("Quest");
    const FName Outcome = TEXT("Outcome");
    const FName Time    = TEXT("Time");
    const FName Source  = TEXT("Source");
}

namespace QuestStateView_Entries_ColumnIDs
{
    const FName Dest    = TEXT("Dest");
    const FName Source  = TEXT("Source");
    const FName Outcome = TEXT("Outcome");
    const FName Time    = TEXT("Time");
}

namespace QuestStateView_Prereqs_ColumnIDs
{
    const FName Quest   = TEXT("Quest");
    const FName Type    = TEXT("Type");
    const FName Status  = TEXT("Status");
    const FName Unmet   = TEXT("Unmet");
}

namespace QuestStateView_Style
{
    const FLinearColor SubduedText = FLinearColor(0.55f, 0.55f, 0.55f);

    static FText FormatTime(double Seconds)
    {
        FNumberFormattingOptions Opts;
        Opts.MinimumFractionalDigits = 2;
        Opts.MaximumFractionalDigits = 2;
        return FText::Format(LOCTEXT("TimeFmt", "{0}s"), FText::AsNumber(Seconds, &Opts));
    }
}


// ── Resolutions row widget ───────────────────────────────────────────────────────────────────────────

class SQuestStateResolutionRowWidget : public SMultiColumnTableRow<TSharedPtr<FQuestStateResolutionRow>>
{
public:
    SLATE_BEGIN_ARGS(SQuestStateResolutionRowWidget) {}
        SLATE_ARGUMENT(TSharedPtr<FQuestStateResolutionRow>, Item)
        SLATE_ATTRIBUTE(FText, HighlightText)
        SLATE_EVENT(FOnQuestStateTagRightClicked, OnTagRightClicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
    {
        Item = InArgs._Item;
        HighlightText = InArgs._HighlightText;
        OnTagRightClicked = InArgs._OnTagRightClicked;
        SMultiColumnTableRow<TSharedPtr<FQuestStateResolutionRow>>::Construct(FSuperRowType::FArguments(), OwnerTable);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
    {
        if (!Item.IsValid()) return SNullWidget::NullWidget;

        // WithStripe wraps the inner cell with the alternating-row border. CellTag (default invalid) opts the cell
        // into right-click capture: when valid AND the user right-clicks, fires OnTagRightClicked with the tag.
        // Returns FReply::Unhandled() so the row's normal LMB selection / RMB context-menu trigger still fires.
        auto WithStripe = [this](const TSharedRef<SWidget>& Inner, const FGameplayTag& CellTag = FGameplayTag()) -> TSharedRef<SWidget>
        {
            return SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                .BorderBackgroundColor(this, &SQuestStateResolutionRowWidget::GetStripeColor)
                .Padding(0)
                .OnMouseButtonDown(FPointerEventHandler::CreateLambda(
                    [this, CellTag](const FGeometry&, const FPointerEvent& Event) -> FReply
                    {
                        if (CellTag.IsValid() && Event.GetEffectingButton() == EKeys::RightMouseButton && OnTagRightClicked.IsBound())
                        {
                            OnTagRightClicked.Execute(CellTag);
                        }
                        return FReply::Unhandled();
                    }))
                [ Inner ];
        };

        if (ColumnName == QuestStateView_Resolutions_ColumnIDs::Quest)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock)
                        .Text(FQuestTagComposer::FormatTagForDisplay(Item->QuestTag.GetTagName()))
                        .HighlightText(HighlightText)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ], Item->QuestTag);
        }
        if (ColumnName == QuestStateView_Resolutions_ColumnIDs::Outcome)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock)
                        .Text(FQuestTagComposer::FormatTagForDisplay(Item->OutcomeTag.GetTagName()))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ], Item->OutcomeTag);
        }
        if (ColumnName == QuestStateView_Resolutions_ColumnIDs::Time)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f)).HAlign(HAlign_Right)
                [
                    SNew(STextBlock)
                        .Text(QuestStateView_Style::FormatTime(Item->ResolutionTime))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]);  // no tag — non-tag column, opts out of cell-aware copy
        }
        if (ColumnName == QuestStateView_Resolutions_ColumnIDs::Source)
        {
            const FText Label = (Item->Source == EQuestResolutionSource::Graph)
                ? LOCTEXT("SourceGraph", "Graph")
                : LOCTEXT("SourceExternal", "External");
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f)).HAlign(HAlign_Right)
                [
                    SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]);  // no tag — non-tag column
        }
        return SNullWidget::NullWidget;
    }

private:
    FSlateColor GetStripeColor() const { return FSimpleCoreEditorWidgetUtils::GetTableRowStripeColor(IndexInList); }
    TSharedPtr<FQuestStateResolutionRow> Item;
    TAttribute<FText> HighlightText;
    FOnQuestStateTagRightClicked OnTagRightClicked;
};


// ── Entries row widget ───────────────────────────────────────────────────────────────────────────────

class SQuestStateEntryRowWidget : public SMultiColumnTableRow<TSharedPtr<FQuestStateEntryRow>>
{
public:
    SLATE_BEGIN_ARGS(SQuestStateEntryRowWidget) {}
        SLATE_ARGUMENT(TSharedPtr<FQuestStateEntryRow>, Item)
        SLATE_ATTRIBUTE(FText, HighlightText)
        SLATE_EVENT(FOnQuestStateTagRightClicked, OnTagRightClicked);
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
    {
        Item = InArgs._Item;
        HighlightText = InArgs._HighlightText;
        OnTagRightClicked = InArgs._OnTagRightClicked;
        SMultiColumnTableRow<TSharedPtr<FQuestStateEntryRow>>::Construct(FSuperRowType::FArguments(), OwnerTable);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
    {
        if (!Item.IsValid()) return SNullWidget::NullWidget;

        auto WithStripe = [this](const TSharedRef<SWidget>& Inner, const FGameplayTag& CellTag = FGameplayTag()) -> TSharedRef<SWidget>
        {
            return SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                .BorderBackgroundColor(this, &SQuestStateEntryRowWidget::GetStripeColor)
                .Padding(0)
                .OnMouseButtonDown(FPointerEventHandler::CreateLambda(
                    [this, CellTag](const FGeometry&, const FPointerEvent& Event) -> FReply
                    {
                        if (CellTag.IsValid() && Event.GetEffectingButton() == EKeys::RightMouseButton && OnTagRightClicked.IsBound())
                        {
                            OnTagRightClicked.Execute(CellTag);
                        }
                        return FReply::Unhandled();
                    }))
                [ Inner ];
        };

        if (ColumnName == QuestStateView_Entries_ColumnIDs::Dest)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock)
                        .Text(FQuestTagComposer::FormatTagForDisplay(Item->DestTag.GetTagName()))
                        .HighlightText(HighlightText)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ], Item->DestTag);
        }
        if (ColumnName == QuestStateView_Entries_ColumnIDs::Source)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock)
                        .Text(FQuestTagComposer::FormatTagForDisplay(Item->SourceQuestTag.GetTagName()))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ], Item->SourceQuestTag);
        }
        if (ColumnName == QuestStateView_Entries_ColumnIDs::Outcome)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock)
                        .Text(FQuestTagComposer::FormatTagForDisplay(Item->IncomingOutcomeTag.GetTagName()))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ], Item->IncomingOutcomeTag);
        }
        if (ColumnName == QuestStateView_Entries_ColumnIDs::Time)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f)).HAlign(HAlign_Right)
                [
                    SNew(STextBlock)
                        .Text(QuestStateView_Style::FormatTime(Item->EntryTime))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]);
        }
        return SNullWidget::NullWidget;
    }

private:
    FSlateColor GetStripeColor() const { return FSimpleCoreEditorWidgetUtils::GetTableRowStripeColor(IndexInList); }
    TSharedPtr<FQuestStateEntryRow> Item;
    TAttribute<FText> HighlightText;
    FOnQuestStateTagRightClicked OnTagRightClicked;
};


// ── Prereq Status row widget ─────────────────────────────────────────────────────────────────────────

class SQuestStatePrereqRowWidget : public SMultiColumnTableRow<TSharedPtr<FQuestStatePrereqRow>>
{
public:
    SLATE_BEGIN_ARGS(SQuestStatePrereqRowWidget) {}
        SLATE_ARGUMENT(TSharedPtr<FQuestStatePrereqRow>, Item)
        SLATE_ATTRIBUTE(FText, HighlightText)
        SLATE_EVENT(FOnQuestStateTagRightClicked, OnTagRightClicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
    {
        Item = InArgs._Item;
        HighlightText = InArgs._HighlightText;
        OnTagRightClicked = InArgs._OnTagRightClicked;

        SMultiColumnTableRow<TSharedPtr<FQuestStatePrereqRow>>::Construct(FSuperRowType::FArguments(), OwnerTable);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
    {
        if (!Item.IsValid()) return SNullWidget::NullWidget;

        auto WithStripe = [this](const TSharedRef<SWidget>& Inner, const FGameplayTag& CellTag = FGameplayTag()) -> TSharedRef<SWidget>
        {
            return SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                .BorderBackgroundColor(this, &SQuestStatePrereqRowWidget::GetStripeColor)
                .Padding(0)
                .OnMouseButtonDown(FPointerEventHandler::CreateLambda(
                    [this, CellTag](const FGeometry&, const FPointerEvent& Event) -> FReply
                    {
                        if (CellTag.IsValid() && Event.GetEffectingButton() == EKeys::RightMouseButton && OnTagRightClicked.IsBound())
                        {
                            OnTagRightClicked.Execute(CellTag);
                        }
                        return FReply::Unhandled();
                    }))
                [ Inner ];
        };

        if (ColumnName == QuestStateView_Prereqs_ColumnIDs::Quest)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock)
                        .Text(FQuestTagComposer::FormatTagForDisplay(Item->QuestTag.GetTagName()))
                        .HighlightText(HighlightText)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ], Item->QuestTag);
        }
        if (ColumnName == QuestStateView_Prereqs_ColumnIDs::Type)
        {
            const FText Label = Item->bIsAlways ? LOCTEXT("TypeAlways", "Always") : LOCTEXT("TypeCustom", "Custom");
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]);
        }
        if (ColumnName == QuestStateView_Prereqs_ColumnIDs::Status)
        {
            const FText Label = Item->bSatisfied ? LOCTEXT("StatusMet", "Met") : LOCTEXT("StatusUnmet", "Unmet");
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]);
        }
        if (ColumnName == QuestStateView_Prereqs_ColumnIDs::Unmet)
        {
            return WithStripe(SNew(SBox).Padding(FMargin(6.f, 2.f)).HAlign(HAlign_Right)
                [
                    SNew(STextBlock)
                        .Text(FText::AsNumber(Item->UnsatisfiedLeafCount))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]);
        }
        return SNullWidget::NullWidget;
    }

private:
    FSlateColor GetStripeColor() const { return FSimpleCoreEditorWidgetUtils::GetTableRowStripeColor(IndexInList); }
    TSharedPtr<FQuestStatePrereqRow> Item;
    TAttribute<FText> HighlightText;
    FOnQuestStateTagRightClicked OnTagRightClicked;
};


// ── SQuestStateView ──────────────────────────────────────────────────────────────────────────────────

void SQuestStateView::Construct(const FArguments& InArgs)
{
    PersistenceKey = InArgs._PersistenceKey;

    // Restore the previously-active tab if a persisted entry exists for this panel. Saved as a named string
    // ("Resolutions" / "Entries" / "PrereqStatus") under the same FactsPanel section that the SFactsPanel
    // uses for view-selection persistence — keys are namespaced by PersistenceKey.
    if (!PersistenceKey.IsNone())
    {
        FString SavedTab;
        GConfig->GetString(TEXT("FactsPanel"),
            *FString::Printf(TEXT("%s.QuestStateActiveTab"), *PersistenceKey.ToString()),
            SavedTab, GEditorPerProjectIni);
        if      (SavedTab == TEXT("Entries"))      { ActiveTab = EQuestStateViewTab::Entries; }
        else if (SavedTab == TEXT("PrereqStatus")) { ActiveTab = EQuestStateViewTab::PrereqStatus; }
        // else: leave at default (EQuestStateViewTab::Resolutions, set in the header default-init).
    }

    // Default sort per tab: each tab's primary tag column ascending. Refresh*FromSubsystem calls Sort* so
    // the initial population respects this.
    ResolutionsSortColumn = QuestStateView_Resolutions_ColumnIDs::Time;
    ResolutionsSortMode   = EColumnSortMode::Descending;
    EntriesSortColumn     = QuestStateView_Entries_ColumnIDs::Time;
    EntriesSortMode       = EColumnSortMode::Descending;
    PrereqsSortColumn     = QuestStateView_Prereqs_ColumnIDs::Quest;
    PrereqsSortMode       = EColumnSortMode::Ascending;

    if (FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel())
    {
        DebugActiveHandle = Channel->OnDebugActiveChanged.AddRaw(this, &SQuestStateView::HandleDebugActiveChanged);
    }

    // Resolutions header + list
    TSharedRef<SHeaderRow> ResolutionsHeader = SNew(SHeaderRow)
        + SHeaderRow::Column(QuestStateView_Resolutions_ColumnIDs::Quest)
            .DefaultLabel(LOCTEXT("ResColQuest", "Quest")).FillWidth(0.40f)
            .SortMode_Lambda([this]() { return GetResolutionsSortMode(QuestStateView_Resolutions_ColumnIDs::Quest); })
            .OnSort(this, &SQuestStateView::HandleResolutionsColumnSort)
        + SHeaderRow::Column(QuestStateView_Resolutions_ColumnIDs::Outcome)
            .DefaultLabel(LOCTEXT("ResColOutcome", "Outcome")).FillWidth(0.30f)
            .SortMode_Lambda([this]() { return GetResolutionsSortMode(QuestStateView_Resolutions_ColumnIDs::Outcome); })
            .OnSort(this, &SQuestStateView::HandleResolutionsColumnSort)
        + SHeaderRow::Column(QuestStateView_Resolutions_ColumnIDs::Time)
            .DefaultLabel(LOCTEXT("ResColTime", "Time")).FillWidth(0.15f).HAlignHeader(HAlign_Right)
            .SortMode_Lambda([this]() { return GetResolutionsSortMode(QuestStateView_Resolutions_ColumnIDs::Time); })
            .OnSort(this, &SQuestStateView::HandleResolutionsColumnSort)
        + SHeaderRow::Column(QuestStateView_Resolutions_ColumnIDs::Source)
            .DefaultLabel(LOCTEXT("ResColSource", "Source")).FillWidth(0.15f).HAlignHeader(HAlign_Right)
            .SortMode_Lambda([this]() { return GetResolutionsSortMode(QuestStateView_Resolutions_ColumnIDs::Source); })
            .OnSort(this, &SQuestStateView::HandleResolutionsColumnSort);

    ResolutionsList = SNew(SListView<TSharedPtr<FQuestStateResolutionRow>>)
        .ListItemsSource(&Resolutions)
        .OnGenerateRow(this, &SQuestStateView::HandleGenerateResolutionRow)
        .OnContextMenuOpening(this, &SQuestStateView::HandleContextMenuOpening)
        .HeaderRow(ResolutionsHeader)
        .SelectionMode(ESelectionMode::Multi);

    // Entries header + list
    TSharedRef<SHeaderRow> EntriesHeader = SNew(SHeaderRow)
        + SHeaderRow::Column(QuestStateView_Entries_ColumnIDs::Dest)
            .DefaultLabel(LOCTEXT("EntColDest", "Destination")).FillWidth(0.32f)
            .SortMode_Lambda([this]() { return GetEntriesSortMode(QuestStateView_Entries_ColumnIDs::Dest); })
            .OnSort(this, &SQuestStateView::HandleEntriesColumnSort)
        + SHeaderRow::Column(QuestStateView_Entries_ColumnIDs::Source)
            .DefaultLabel(LOCTEXT("EntColSource", "Source")).FillWidth(0.30f)
            .SortMode_Lambda([this]() { return GetEntriesSortMode(QuestStateView_Entries_ColumnIDs::Source); })
            .OnSort(this, &SQuestStateView::HandleEntriesColumnSort)
        + SHeaderRow::Column(QuestStateView_Entries_ColumnIDs::Outcome)
            .DefaultLabel(LOCTEXT("EntColOutcome", "Outcome")).FillWidth(0.23f)
            .SortMode_Lambda([this]() { return GetEntriesSortMode(QuestStateView_Entries_ColumnIDs::Outcome); })
            .OnSort(this, &SQuestStateView::HandleEntriesColumnSort)
        + SHeaderRow::Column(QuestStateView_Entries_ColumnIDs::Time)
            .DefaultLabel(LOCTEXT("EntColTime", "Time")).FillWidth(0.15f).HAlignHeader(HAlign_Right)
            .SortMode_Lambda([this]() { return GetEntriesSortMode(QuestStateView_Entries_ColumnIDs::Time); })
            .OnSort(this, &SQuestStateView::HandleEntriesColumnSort);

    EntriesList = SNew(SListView<TSharedPtr<FQuestStateEntryRow>>)
        .ListItemsSource(&Entries)
        .OnGenerateRow(this, &SQuestStateView::HandleGenerateEntryRow)
        .OnContextMenuOpening(this, &SQuestStateView::HandleContextMenuOpening)
        .HeaderRow(EntriesHeader)
        .SelectionMode(ESelectionMode::Multi);

    // Prereq Status header + list
    TSharedRef<SHeaderRow> PrereqsHeader = SNew(SHeaderRow)
        + SHeaderRow::Column(QuestStateView_Prereqs_ColumnIDs::Quest)
            .DefaultLabel(LOCTEXT("PrqColQuest", "Quest")).FillWidth(0.55f)
            .SortMode_Lambda([this]() { return GetPrereqsSortMode(QuestStateView_Prereqs_ColumnIDs::Quest); })
            .OnSort(this, &SQuestStateView::HandlePrereqsColumnSort)
        + SHeaderRow::Column(QuestStateView_Prereqs_ColumnIDs::Type)
            .DefaultLabel(LOCTEXT("PrqColType", "Type")).FillWidth(0.15f)
            .SortMode_Lambda([this]() { return GetPrereqsSortMode(QuestStateView_Prereqs_ColumnIDs::Type); })
            .OnSort(this, &SQuestStateView::HandlePrereqsColumnSort)
        + SHeaderRow::Column(QuestStateView_Prereqs_ColumnIDs::Status)
            .DefaultLabel(LOCTEXT("PrqColStatus", "Status")).FillWidth(0.15f)
            .SortMode_Lambda([this]() { return GetPrereqsSortMode(QuestStateView_Prereqs_ColumnIDs::Status); })
            .OnSort(this, &SQuestStateView::HandlePrereqsColumnSort)
        + SHeaderRow::Column(QuestStateView_Prereqs_ColumnIDs::Unmet)
            .DefaultLabel(LOCTEXT("PrqColUnmet", "Unmet")).FillWidth(0.15f).HAlignHeader(HAlign_Right)
            .SortMode_Lambda([this]() { return GetPrereqsSortMode(QuestStateView_Prereqs_ColumnIDs::Unmet); })
            .OnSort(this, &SQuestStateView::HandlePrereqsColumnSort);

    PrereqsList = SNew(SListView<TSharedPtr<FQuestStatePrereqRow>>)
        .ListItemsSource(&Prereqs)
        .OnGenerateRow(this, &SQuestStateView::HandleGeneratePrereqRow)
        .OnContextMenuOpening(this, &SQuestStateView::HandleContextMenuOpening)
        .HeaderRow(PrereqsHeader)
        .SelectionMode(ESelectionMode::Multi);

    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
        [
            // 2x2 grid row 1: Status text fills left; segmented tabs auto-width on the right. Tabs get a small
            // left-padding to separate them from the status text.
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                    .Text_Lambda([this]() { return GetStatusText(); })
                    .ColorAndOpacity(FSlateColor(QuestStateView_Style::SubduedText))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
            [
                SNew(SSegmentedControl<EQuestStateViewTab>)
                    .UniformPadding(FMargin(20.f, 4.f))
                    .Value_Lambda([this]() { return ActiveTab; })
                    .OnValueChanged(this, &SQuestStateView::HandleTabChanged)
                    + SSegmentedControl<EQuestStateViewTab>::Slot(EQuestStateViewTab::Resolutions)
                        .Text(LOCTEXT("TabResolutions", "Resolutions"))
                    + SSegmentedControl<EQuestStateViewTab>::Slot(EQuestStateViewTab::Entries)
                        .Text(LOCTEXT("TabEntries", "Entries"))
                    + SSegmentedControl<EQuestStateViewTab>::Slot(EQuestStateViewTab::PrereqStatus)
                        .Text(LOCTEXT("TabPrereqStatus", "Prereq Status"))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
        [
            // 2x2 grid row 2: Session selector (Phase 3, not yet added) will land in an auto-width left slot;
            // for now the filter fills the whole row alone.
            SNew(SSearchBox)
                .HintText(LOCTEXT("FilterHint", "Filter by tag..."))
                .OnTextChanged(this, &SQuestStateView::HandleFilterTextChanged)
        ]
        + SVerticalBox::Slot().FillHeight(1.f)
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                SNew(SBox).Visibility_Lambda([this]() { return GetTabVisibility(EQuestStateViewTab::Resolutions); })
                    [ ResolutionsList.ToSharedRef() ]
            ]
            + SOverlay::Slot()
            [
                SNew(SBox).Visibility_Lambda([this]() { return GetTabVisibility(EQuestStateViewTab::Entries); })
                    [ EntriesList.ToSharedRef() ]
            ]
            + SOverlay::Slot()
            [
                SNew(SBox).Visibility_Lambda([this]() { return GetTabVisibility(EQuestStateViewTab::PrereqStatus); })
                    [ PrereqsList.ToSharedRef() ]
            ]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                    .Text_Lambda([this]() { return GetEmptyMessageText(); })
                    .ColorAndOpacity(FSlateColor(QuestStateView_Style::SubduedText))
                    .Visibility_Lambda([this]() { return GetEmptyMessageVisibility(); })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
            ]
        ]
    ];

    RebuildAll();
}

SQuestStateView::~SQuestStateView()
{
    if (DebugActiveHandle.IsValid())
    {
        if (FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel())
        {
            Channel->OnDebugActiveChanged.Remove(DebugActiveHandle);
        }
        DebugActiveHandle.Reset();
    }
}

void SQuestStateView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    if (Channel && Channel->IsActive() && Channel->GetQuestStateSubsystem())
    {
        if (RefreshResolutionsFromSubsystem() && ResolutionsList.IsValid()) ResolutionsList->RequestListRefresh();
        if (RefreshEntriesFromSubsystem()     && EntriesList.IsValid())     EntriesList->RequestListRefresh();
        if (RefreshPrereqsFromSubsystem()     && PrereqsList.IsValid())     PrereqsList->RequestListRefresh();

        // Capture current game time so the snapshot status text can show "last seen at t=X.XXs" once PIE ends.
        LastSeenGameTime = Channel->GetCurrentGameTimeSeconds();
    }
    // No else-branch: when PIE ends, the snapshot stays visible until the next PIE session starts. Wipe-and-refresh
    // happens in HandleDebugActiveChanged on PostPIEStarted only.
}

void SQuestStateView::HandleDebugActiveChanged()
{
    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    if (Channel && Channel->IsActive() && Channel->GetQuestStateSubsystem())
    {
        // PIE just started — wipe any snapshot from the previous session and pull fresh data.
        RebuildAll();
    }
    // PIE just ended — leave the snapshot intact so the designer can inspect post-session row history. The next
    // PostPIEStarted will RebuildAll and replace it with the fresh session.
}

void SQuestStateView::RebuildAll()
{
    AllResolutions.Reset(); Resolutions.Reset();
    AllEntries.Reset();     Entries.Reset();
    AllPrereqs.Reset();     Prereqs.Reset();

    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    if (!Channel || !Channel->IsActive() || !Channel->GetQuestStateSubsystem()) return;

    RefreshResolutionsFromSubsystem();
    RefreshEntriesFromSubsystem();
    RefreshPrereqsFromSubsystem();

    if (ResolutionsList.IsValid()) ResolutionsList->RequestListRefresh();
    if (EntriesList.IsValid())     EntriesList->RequestListRefresh();
    if (PrereqsList.IsValid())     PrereqsList->RequestListRefresh();
}

void SQuestStateView::HandleTabChanged(EQuestStateViewTab NewTab)
{
    ActiveTab = NewTab;
    // No data work — visibility lambdas swap which list shows; status / empty lambdas re-evaluate from bindings.

    // Persist the active tab so the next construction (e.g., editor restart) restores it. Same per-panel key
    // namespace as SFactsPanel's view-selection persistence.
    if (!PersistenceKey.IsNone())
    {
        FString TabName;
        switch (NewTab)
        {
        case EQuestStateViewTab::Resolutions:  TabName = TEXT("Resolutions");  break;
        case EQuestStateViewTab::Entries:      TabName = TEXT("Entries");      break;
        case EQuestStateViewTab::PrereqStatus: TabName = TEXT("PrereqStatus"); break;
        }
        GConfig->SetString(TEXT("FactsPanel"),
            *FString::Printf(TEXT("%s.QuestStateActiveTab"), *PersistenceKey.ToString()),
            *TabName, GEditorPerProjectIni);
        GConfig->Flush(false, GEditorPerProjectIni);
    }
}

EVisibility SQuestStateView::GetTabVisibility(EQuestStateViewTab Tab) const
{
    return (ActiveTab == Tab) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SQuestStateView::GetEmptyMessageVisibility() const
{
    switch (ActiveTab)
    {
    case EQuestStateViewTab::Resolutions:  return Resolutions.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
    case EQuestStateViewTab::Entries:      return Entries.IsEmpty()     ? EVisibility::Visible : EVisibility::Collapsed;
    case EQuestStateViewTab::PrereqStatus: return Prereqs.IsEmpty()     ? EVisibility::Visible : EVisibility::Collapsed;
    }
    return EVisibility::Collapsed;
}

EVisibility SQuestStateView::GetListVisibility() const
{
    return GetEmptyMessageVisibility() == EVisibility::Visible ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SQuestStateView::GetEmptyMessageText() const
{
    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    if (!Channel || !Channel->IsActive() || !Channel->GetQuestStateSubsystem())
    {
        return LOCTEXT("EmptyNotInPIE", "Not in PIE — start Play In Editor to inspect live Quest State.");
    }
    const bool bUnfilteredEmpty =
        (ActiveTab == EQuestStateViewTab::Resolutions  && AllResolutions.IsEmpty()) ||
        (ActiveTab == EQuestStateViewTab::Entries      && AllEntries.IsEmpty())     ||
        (ActiveTab == EQuestStateViewTab::PrereqStatus && AllPrereqs.IsEmpty());
    if (bUnfilteredEmpty)
    {
        switch (ActiveTab)
        {
        case EQuestStateViewTab::Resolutions:  return LOCTEXT("EmptyNoResolutions", "PIE active — no quests have resolved yet.");
        case EQuestStateViewTab::Entries:      return LOCTEXT("EmptyNoEntries",     "PIE active — no quests have been entered yet.");
        case EQuestStateViewTab::PrereqStatus: return LOCTEXT("EmptyNoPrereqs",     "PIE active — no quests are currently in PendingGiver state.");
        }
    }
    return FText::Format(LOCTEXT("EmptyFilterMismatch", "No rows match filter '{0}'."), FText::FromString(FilterText));
}

FText SQuestStateView::GetStatusText() const
{
    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    const bool bPIEActive = Channel && Channel->IsActive() && Channel->GetQuestStateSubsystem();

    int32 AllNum = 0;
    int32 ShownNum = 0;
    switch (ActiveTab)
    {
    case EQuestStateViewTab::Resolutions:  AllNum = AllResolutions.Num(); ShownNum = Resolutions.Num(); break;
    case EQuestStateViewTab::Entries:      AllNum = AllEntries.Num();     ShownNum = Entries.Num();     break;
    case EQuestStateViewTab::PrereqStatus: AllNum = AllPrereqs.Num();     ShownNum = Prereqs.Num();     break;
    }

    if (!bPIEActive)
    {
        // Snapshot mode: rows are preserved from the just-ended session. Show the last-seen game time so post-
        // session row timestamps have a reference point. If nothing was ever captured, fall through to "idle".
        const bool bHasSnapshot = !AllResolutions.IsEmpty() || !AllEntries.IsEmpty() || !AllPrereqs.IsEmpty();
        if (bHasSnapshot)
        {
            return FText::Format(LOCTEXT("StatusSnapshot", "SNAPSHOT — last PIE session ended at t={0} — {1} row(s)"),
                QuestStateView_Style::FormatTime(LastSeenGameTime), FText::AsNumber(AllNum));
        }
        return LOCTEXT("StatusIdle", "idle");
    }

    // PIE active — live readout including current game time. Time_Lambda binding causes this to repaint every frame
    // while the panel is visible, so the time field updates in real time.
    const FText TimeText = QuestStateView_Style::FormatTime(Channel->GetCurrentGameTimeSeconds());
    if (AllNum != ShownNum)
    {
        return FText::Format(LOCTEXT("StatusActiveFiltered", "DEBUG (PIE) — t={0} — {1} row(s), {2} shown"),
            TimeText, FText::AsNumber(AllNum), FText::AsNumber(ShownNum));
    }
    return FText::Format(LOCTEXT("StatusActive", "DEBUG (PIE) — t={0} — {1} row(s)"),
        TimeText, FText::AsNumber(AllNum));
}

void SQuestStateView::HandleFilterTextChanged(const FText& NewText)
{
    FilterText = NewText.ToString();
    ApplyAllFilters();
    if (ResolutionsList.IsValid()) ResolutionsList->RequestListRefresh();
    if (EntriesList.IsValid())     EntriesList->RequestListRefresh();
    if (PrereqsList.IsValid())     PrereqsList->RequestListRefresh();
}

void SQuestStateView::ApplyAllFilters()
{
    ApplyResolutionsFilter();
    ApplyEntriesFilter();
    ApplyPrereqsFilter();
}


// ── Per-tab refresh ──────────────────────────────────────────────────────────────────────────────────

bool SQuestStateView::RefreshResolutionsFromSubsystem()
{
    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    UQuestStateSubsystem* Subsystem = Channel ? Channel->GetQuestStateSubsystem() : nullptr;
    if (!Subsystem) return false;

    const TMap<FGameplayTag, FQuestResolutionRecord>& Map = Subsystem->GetAllResolutions();

    // Snapshot diff: total history-entry count across all quests is a cheap proxy for "anything changed."
    // Rebuild only when count differs from cached. Append-only history means count-only diff is reliable.
    int32 TotalCount = 0;
    for (const TPair<FGameplayTag, FQuestResolutionRecord>& Pair : Map) { TotalCount += Pair.Value.History.Num(); }
    if (TotalCount == AllResolutions.Num()) return false;

    AllResolutions.Reset(TotalCount);
    for (const TPair<FGameplayTag, FQuestResolutionRecord>& Pair : Map)
    {
        for (const FQuestResolutionEntry& Entry : Pair.Value.History)
        {
            TSharedPtr<FQuestStateResolutionRow> Row = MakeShared<FQuestStateResolutionRow>();
            Row->QuestTag       = Pair.Key;
            Row->OutcomeTag     = Entry.OutcomeTag;
            Row->ResolutionTime = Entry.ResolutionTime;
            Row->Source         = Entry.Source;
            AllResolutions.Add(MoveTemp(Row));
        }
    }
    SortResolutions();
    ApplyResolutionsFilter();
    return true;
}

bool SQuestStateView::RefreshEntriesFromSubsystem()
{
    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    UQuestStateSubsystem* Subsystem = Channel ? Channel->GetQuestStateSubsystem() : nullptr;
    if (!Subsystem) return false;

    const TMap<FGameplayTag, FQuestEntryRecord>& Map = Subsystem->GetAllEntries();

    int32 TotalCount = 0;
    for (const TPair<FGameplayTag, FQuestEntryRecord>& Pair : Map) { TotalCount += Pair.Value.History.Num(); }
    if (TotalCount == AllEntries.Num()) return false;

    AllEntries.Reset(TotalCount);
    for (const TPair<FGameplayTag, FQuestEntryRecord>& Pair : Map)
    {
        for (const FQuestEntryArrival& Entry : Pair.Value.History)
        {
            TSharedPtr<FQuestStateEntryRow> Row = MakeShared<FQuestStateEntryRow>();
            Row->DestTag            = Pair.Key;
            Row->SourceQuestTag     = Entry.SourceQuestTag;
            Row->IncomingOutcomeTag = Entry.IncomingOutcomeTag;
            Row->EntryTime          = Entry.EntryTime;
            AllEntries.Add(MoveTemp(Row));
        }
    }
    SortEntries();
    ApplyEntriesFilter();
    return true;
}

bool SQuestStateView::RefreshPrereqsFromSubsystem()
{
    FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel();
    UQuestStateSubsystem* Subsystem = Channel ? Channel->GetQuestStateSubsystem() : nullptr;
    if (!Subsystem) return false;

    const TMap<FGameplayTag, FQuestPrereqStatus>& Map = Subsystem->GetAllCachedPrereqStatus();

    // Always rebuild — prereq snapshots can mutate internally (bSatisfied flips, leaf counts shift) without
    // changing the map size, so a count diff would miss those updates. Map is bounded by quests-in-PendingGiver
    // count (typically small). If this becomes a hot path, hash the map's contents for the diff key.
    AllPrereqs.Reset(Map.Num());
    for (const TPair<FGameplayTag, FQuestPrereqStatus>& Pair : Map)
    {
        TSharedPtr<FQuestStatePrereqRow> Row = MakeShared<FQuestStatePrereqRow>();
        Row->QuestTag   = Pair.Key;
        Row->bIsAlways  = Pair.Value.bIsAlways;
        Row->bSatisfied = Pair.Value.bSatisfied;
        int32 UnsatCount = 0;
        for (const FQuestPrereqLeafStatus& Leaf : Pair.Value.Leaves)
        {
            if (!Leaf.bSatisfied) ++UnsatCount;
        }
        Row->UnsatisfiedLeafCount = UnsatCount;
        AllPrereqs.Add(MoveTemp(Row));
    }
    SortPrereqs();
    ApplyPrereqsFilter();
    return true;
}


// ── Per-tab sort ─────────────────────────────────────────────────────────────────────────────────────

void SQuestStateView::SortResolutions()
{
    if (ResolutionsSortMode == EColumnSortMode::None) return;
    const bool bAsc = (ResolutionsSortMode == EColumnSortMode::Ascending);

    if (ResolutionsSortColumn == QuestStateView_Resolutions_ColumnIDs::Quest)
    {
        AllResolutions.Sort([bAsc](const TSharedPtr<FQuestStateResolutionRow>& A, const TSharedPtr<FQuestStateResolutionRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->QuestTag.GetTagName().LexicalLess(B->QuestTag.GetTagName())
                        : B->QuestTag.GetTagName().LexicalLess(A->QuestTag.GetTagName());
        });
    }
    else if (ResolutionsSortColumn == QuestStateView_Resolutions_ColumnIDs::Outcome)
    {
        AllResolutions.Sort([bAsc](const TSharedPtr<FQuestStateResolutionRow>& A, const TSharedPtr<FQuestStateResolutionRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->OutcomeTag.GetTagName().LexicalLess(B->OutcomeTag.GetTagName())
                        : B->OutcomeTag.GetTagName().LexicalLess(A->OutcomeTag.GetTagName());
        });
    }
    else if (ResolutionsSortColumn == QuestStateView_Resolutions_ColumnIDs::Time)
    {
        AllResolutions.Sort([bAsc](const TSharedPtr<FQuestStateResolutionRow>& A, const TSharedPtr<FQuestStateResolutionRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->ResolutionTime < B->ResolutionTime : A->ResolutionTime > B->ResolutionTime;
        });
    }
    else if (ResolutionsSortColumn == QuestStateView_Resolutions_ColumnIDs::Source)
    {
        AllResolutions.Sort([bAsc](const TSharedPtr<FQuestStateResolutionRow>& A, const TSharedPtr<FQuestStateResolutionRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? static_cast<uint8>(A->Source) < static_cast<uint8>(B->Source)
                        : static_cast<uint8>(A->Source) > static_cast<uint8>(B->Source);
        });
    }
}

void SQuestStateView::SortEntries()
{
    if (EntriesSortMode == EColumnSortMode::None) return;
    const bool bAsc = (EntriesSortMode == EColumnSortMode::Ascending);

    if (EntriesSortColumn == QuestStateView_Entries_ColumnIDs::Dest)
    {
        AllEntries.Sort([bAsc](const TSharedPtr<FQuestStateEntryRow>& A, const TSharedPtr<FQuestStateEntryRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->DestTag.GetTagName().LexicalLess(B->DestTag.GetTagName())
                        : B->DestTag.GetTagName().LexicalLess(A->DestTag.GetTagName());
        });
    }
    else if (EntriesSortColumn == QuestStateView_Entries_ColumnIDs::Source)
    {
        AllEntries.Sort([bAsc](const TSharedPtr<FQuestStateEntryRow>& A, const TSharedPtr<FQuestStateEntryRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->SourceQuestTag.GetTagName().LexicalLess(B->SourceQuestTag.GetTagName())
                        : B->SourceQuestTag.GetTagName().LexicalLess(A->SourceQuestTag.GetTagName());
        });
    }
    else if (EntriesSortColumn == QuestStateView_Entries_ColumnIDs::Outcome)
    {
        AllEntries.Sort([bAsc](const TSharedPtr<FQuestStateEntryRow>& A, const TSharedPtr<FQuestStateEntryRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->IncomingOutcomeTag.GetTagName().LexicalLess(B->IncomingOutcomeTag.GetTagName())
                        : B->IncomingOutcomeTag.GetTagName().LexicalLess(A->IncomingOutcomeTag.GetTagName());
        });
    }
    else if (EntriesSortColumn == QuestStateView_Entries_ColumnIDs::Time)
    {
        AllEntries.Sort([bAsc](const TSharedPtr<FQuestStateEntryRow>& A, const TSharedPtr<FQuestStateEntryRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->EntryTime < B->EntryTime : A->EntryTime > B->EntryTime;
        });
    }
}

void SQuestStateView::SortPrereqs()
{
    if (PrereqsSortMode == EColumnSortMode::None) return;
    const bool bAsc = (PrereqsSortMode == EColumnSortMode::Ascending);

    if (PrereqsSortColumn == QuestStateView_Prereqs_ColumnIDs::Quest)
    {
        AllPrereqs.Sort([bAsc](const TSharedPtr<FQuestStatePrereqRow>& A, const TSharedPtr<FQuestStatePrereqRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->QuestTag.GetTagName().LexicalLess(B->QuestTag.GetTagName())
                        : B->QuestTag.GetTagName().LexicalLess(A->QuestTag.GetTagName());
        });
    }
    else if (PrereqsSortColumn == QuestStateView_Prereqs_ColumnIDs::Type)
    {
        AllPrereqs.Sort([bAsc](const TSharedPtr<FQuestStatePrereqRow>& A, const TSharedPtr<FQuestStatePrereqRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->bIsAlways < B->bIsAlways : A->bIsAlways > B->bIsAlways;
        });
    }
    else if (PrereqsSortColumn == QuestStateView_Prereqs_ColumnIDs::Status)
    {
        AllPrereqs.Sort([bAsc](const TSharedPtr<FQuestStatePrereqRow>& A, const TSharedPtr<FQuestStatePrereqRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->bSatisfied < B->bSatisfied : A->bSatisfied > B->bSatisfied;
        });
    }
    else if (PrereqsSortColumn == QuestStateView_Prereqs_ColumnIDs::Unmet)
    {
        AllPrereqs.Sort([bAsc](const TSharedPtr<FQuestStatePrereqRow>& A, const TSharedPtr<FQuestStatePrereqRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAsc ? A->UnsatisfiedLeafCount < B->UnsatisfiedLeafCount : A->UnsatisfiedLeafCount > B->UnsatisfiedLeafCount;
        });
    }
}


// ── Per-tab apply filter ─────────────────────────────────────────────────────────────────────────────

void SQuestStateView::ApplyResolutionsFilter()
{
    Resolutions.Reset();
    if (FilterText.IsEmpty()) { Resolutions.Append(AllResolutions); return; }

    // Substring-match against tag-typed columns only. Time / Source are deliberately excluded — filter is for
    // tag identity, not display values. Default Contains is case-insensitive.
    for (const TSharedPtr<FQuestStateResolutionRow>& Row : AllResolutions)
    {
        if (!Row.IsValid()) continue;
        if (Row->QuestTag.GetTagName().ToString().Contains(FilterText) ||
            Row->OutcomeTag.GetTagName().ToString().Contains(FilterText))
        {
            Resolutions.Add(Row);
        }
    }
}

void SQuestStateView::ApplyEntriesFilter()
{
    Entries.Reset();
    if (FilterText.IsEmpty()) { Entries.Append(AllEntries); return; }

    for (const TSharedPtr<FQuestStateEntryRow>& Row : AllEntries)
    {
        if (!Row.IsValid()) continue;
        if (Row->DestTag.GetTagName().ToString().Contains(FilterText)            ||
            Row->SourceQuestTag.GetTagName().ToString().Contains(FilterText)     ||
            Row->IncomingOutcomeTag.GetTagName().ToString().Contains(FilterText))
        {
            Entries.Add(Row);
        }
    }
}

void SQuestStateView::ApplyPrereqsFilter()
{
    Prereqs.Reset();
    if (FilterText.IsEmpty()) { Prereqs.Append(AllPrereqs); return; }
    for (const TSharedPtr<FQuestStatePrereqRow>& Row : AllPrereqs)
    {
        if (Row.IsValid() && Row->QuestTag.GetTagName().ToString().Contains(FilterText))
        {
            Prereqs.Add(Row);
        }
    }
}


// ── Per-tab generate row ─────────────────────────────────────────────────────────────────────────────

TSharedRef<ITableRow> SQuestStateView::HandleGenerateResolutionRow(TSharedPtr<FQuestStateResolutionRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SQuestStateResolutionRowWidget, OwnerTable)
        .Item(Item)
        .HighlightText(this, &SQuestStateView::GetFilterTextAsText)
        .OnTagRightClicked(FOnQuestStateTagRightClicked::CreateSP(this, &SQuestStateView::NotifyTagRightClicked));
}

TSharedRef<ITableRow> SQuestStateView::HandleGenerateEntryRow(TSharedPtr<FQuestStateEntryRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SQuestStateEntryRowWidget, OwnerTable)
        .Item(Item)
        .HighlightText(this, &SQuestStateView::GetFilterTextAsText)
        .OnTagRightClicked(FOnQuestStateTagRightClicked::CreateSP(this, &SQuestStateView::NotifyTagRightClicked));

}

TSharedRef<ITableRow> SQuestStateView::HandleGeneratePrereqRow(TSharedPtr<FQuestStatePrereqRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SQuestStatePrereqRowWidget, OwnerTable)
        .Item(Item)
        .HighlightText(this, &SQuestStateView::GetFilterTextAsText)
        .OnTagRightClicked(FOnQuestStateTagRightClicked::CreateSP(this, &SQuestStateView::NotifyTagRightClicked));
}


// ── Context menu / copy ──────────────────────────────────────────────────────────────────────────────

TSharedPtr<SWidget> SQuestStateView::HandleContextMenuOpening()
{
    int32 NumSelected = 0;
    switch (ActiveTab)
    {
    case EQuestStateViewTab::Resolutions:  NumSelected = ResolutionsList.IsValid() ? ResolutionsList->GetNumItemsSelected() : 0; break;
    case EQuestStateViewTab::Entries:      NumSelected = EntriesList.IsValid()     ? EntriesList->GetNumItemsSelected()     : 0; break;
    case EQuestStateViewTab::PrereqStatus: NumSelected = PrereqsList.IsValid()     ? PrereqsList->GetNumItemsSelected()     : 0; break;
    }
    if (NumSelected == 0) { bLastRightClickedTagSet = false; return nullptr; }

    // Snapshot + clear the cell-aware right-click capture. The flag is set by per-cell OnMouseButtonDown handlers
    // earlier in this same right-click event (the cell handler runs before the row's context-menu trigger fires up
    // through SListView). Clearing here ensures a future right-click on a non-tag cell doesn't reuse stale state.
    const bool       bHaveSpecificTag      = bLastRightClickedTagSet;
    const FGameplayTag SpecificTag         = LastRightClickedTag;
    bLastRightClickedTagSet = false;

    FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/true, /*CommandList=*/nullptr);
    MenuBuilder.BeginSection(NAME_None, LOCTEXT("ClipboardSection", "Clipboard"));

    if (bHaveSpecificTag && SpecificTag.IsValid())
    {
        // Show the actual tag value in the label so the user sees exactly what they'd be copying.
        const FText TagDisplay = FText::FromName(SpecificTag.GetTagName());
        const FText CopyLabel  = FText::Format(LOCTEXT("CopyTagFmt", "Copy '{0}'"), TagDisplay);
        const FText CopyTooltip = FText::Format(
            LOCTEXT("CopyTagTooltipFmt", "Copy the full tag '{0}' to the clipboard."), TagDisplay);
        MenuBuilder.AddMenuEntry(
            CopyLabel,
            CopyTooltip,
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(this, &SQuestStateView::CopyRightClickedTag, SpecificTag)));
    }

    MenuBuilder.AddMenuEntry(
        LOCTEXT("CopyRowsTSV", "Copy Row(s) as TSV"),
        LOCTEXT("CopyRowsTSVTooltip", "Copy each selected row as tab-separated values across all visible columns. Paste into a spreadsheet for analysis."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(this, &SQuestStateView::CopySelectedRowsAsTSV)));

    MenuBuilder.EndSection();
    return MenuBuilder.MakeWidget();
}

void SQuestStateView::CopyRightClickedTag(FGameplayTag InTag)
{
    if (!InTag.IsValid()) return;
    FPlatformApplicationMisc::ClipboardCopy(*InTag.GetTagName().ToString());
}

void SQuestStateView::CopySelectedRowsAsTSV()
{
    TArray<FString> Lines;
    switch (ActiveTab)
    {
    case EQuestStateViewTab::Resolutions:
        if (ResolutionsList.IsValid())
        {
            Lines.Add(TEXT("Quest\tOutcome\tTime\tSource"));
            for (const TSharedPtr<FQuestStateResolutionRow>& Row : Resolutions)
            {
                if (!Row.IsValid() || !ResolutionsList->IsItemSelected(Row)) continue;
                Lines.Add(FString::Printf(TEXT("%s\t%s\t%.2f\t%s"),
                    *Row->QuestTag.GetTagName().ToString(),
                    *Row->OutcomeTag.GetTagName().ToString(),
                    Row->ResolutionTime,
                    Row->Source == EQuestResolutionSource::Graph ? TEXT("Graph") : TEXT("External")));
            }
        }
        break;
    case EQuestStateViewTab::Entries:
        if (EntriesList.IsValid())
        {
            Lines.Add(TEXT("Destination\tSource\tOutcome\tTime"));
            for (const TSharedPtr<FQuestStateEntryRow>& Row : Entries)
            {
                if (!Row.IsValid() || !EntriesList->IsItemSelected(Row)) continue;
                Lines.Add(FString::Printf(TEXT("%s\t%s\t%s\t%.2f"),
                    *Row->DestTag.GetTagName().ToString(),
                    *Row->SourceQuestTag.GetTagName().ToString(),
                    *Row->IncomingOutcomeTag.GetTagName().ToString(),
                    Row->EntryTime));
            }
        }
        break;
    case EQuestStateViewTab::PrereqStatus:
        if (PrereqsList.IsValid())
        {
            Lines.Add(TEXT("Quest\tType\tStatus\tUnmet"));
            for (const TSharedPtr<FQuestStatePrereqRow>& Row : Prereqs)
            {
                if (!Row.IsValid() || !PrereqsList->IsItemSelected(Row)) continue;
                Lines.Add(FString::Printf(TEXT("%s\t%s\t%s\t%d"),
                    *Row->QuestTag.GetTagName().ToString(),
                    Row->bIsAlways ? TEXT("Always") : TEXT("Custom"),
                    Row->bSatisfied ? TEXT("Met") : TEXT("Unmet"),
                    Row->UnsatisfiedLeafCount));
            }
        }
        break;
    }
    if (Lines.Num() <= 1) return;  // header only, no rows selected
    FPlatformApplicationMisc::ClipboardCopy(*FString::Join(Lines, TEXT("\n")));
}


// ── Per-tab sort wiring ──────────────────────────────────────────────────────────────────────────────

EColumnSortMode::Type SQuestStateView::GetResolutionsSortMode(FName ColumnID) const
{
    return ColumnID == ResolutionsSortColumn ? ResolutionsSortMode : EColumnSortMode::None;
}

EColumnSortMode::Type SQuestStateView::GetEntriesSortMode(FName ColumnID) const
{
    return ColumnID == EntriesSortColumn ? EntriesSortMode : EColumnSortMode::None;
}

EColumnSortMode::Type SQuestStateView::GetPrereqsSortMode(FName ColumnID) const
{
    return ColumnID == PrereqsSortColumn ? PrereqsSortMode : EColumnSortMode::None;
}

void SQuestStateView::HandleResolutionsColumnSort(EColumnSortPriority::Type, const FName& ColumnID, EColumnSortMode::Type NewMode)
{
    ResolutionsSortColumn = ColumnID; ResolutionsSortMode = NewMode;
    SortResolutions(); ApplyResolutionsFilter();
    if (ResolutionsList.IsValid()) ResolutionsList->RequestListRefresh();
}

void SQuestStateView::HandleEntriesColumnSort(EColumnSortPriority::Type, const FName& ColumnID, EColumnSortMode::Type NewMode)
{
    EntriesSortColumn = ColumnID; EntriesSortMode = NewMode;
    SortEntries(); ApplyEntriesFilter();
    if (EntriesList.IsValid()) EntriesList->RequestListRefresh();
}

void SQuestStateView::HandlePrereqsColumnSort(EColumnSortPriority::Type, const FName& ColumnID, EColumnSortMode::Type NewMode)
{
    PrereqsSortColumn = ColumnID; PrereqsSortMode = NewMode;
    SortPrereqs(); ApplyPrereqsFilter();
    if (PrereqsList.IsValid()) PrereqsList->RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE