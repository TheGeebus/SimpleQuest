// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SWorldStateFactsView.h"

#include "Debug/SimpleCorePIEDebugChannel.h"
#include "SimpleCoreEditor.h"
#include "SimpleCoreEditorLog.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Input/SSearchBox.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SimpleCoreEditorWidgetUtils.h"

#define LOCTEXT_NAMESPACE "SWorldStateFactsView"

namespace WorldStateFactsView_ColumnIDs
{
    const FName Tag   = TEXT("Tag");
    const FName Count = TEXT("Count");
}

namespace WorldStateFactsView_Style
{
    const FLinearColor SubduedText = FLinearColor(0.55f, 0.55f, 0.55f);
}

class SWorldStateFactRow : public SMultiColumnTableRow<TSharedPtr<FWorldStateFactRow>>
{
public:
    SLATE_BEGIN_ARGS(SWorldStateFactRow) {}
    SLATE_ARGUMENT(TSharedPtr<FWorldStateFactRow>, Item)
    SLATE_ATTRIBUTE(FText, HighlightText)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
    {
        Item = InArgs._Item;
        HighlightText = InArgs._HighlightText;

        SMultiColumnTableRow<TSharedPtr<FWorldStateFactRow>>::Construct(FSuperRowType::FArguments(), OwnerTable);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
    {
        if (!Item.IsValid())
        {
            return SNullWidget::NullWidget;
        }

        // Per-cell zebra stripe. Mirrors SStaleQuestTagsPanel's row pattern (factored helper for the color value lives
        // in FSimpleCoreEditorWidgetUtils). Delegate-bound BorderBackgroundColor reads STableRow::IndexInList per paint.
        auto WithStripe = [this](const TSharedRef<SWidget>& Inner) -> TSharedRef<SWidget>
        {
            return SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                .BorderBackgroundColor(this, &SWorldStateFactRow::GetStripeColor)
                .Padding(0)
                [ Inner ];
        };

        if (ColumnName == WorldStateFactsView_ColumnIDs::Tag)
        {
            return WithStripe(
                SNew(SBox).Padding(FMargin(6.f, 2.f))
                [
                    SNew(STextBlock)
                        .Text(FText::FromName(Item->Tag.GetTagName()))
                        .HighlightText(HighlightText)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]);
        }
        if (ColumnName == WorldStateFactsView_ColumnIDs::Count)
        {
            return WithStripe(
                SNew(SBox).Padding(FMargin(6.f, 2.f)).HAlign(HAlign_Right)
                [
                    SNew(STextBlock)
                        .Text(FText::AsNumber(Item->Count))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                ]);
        }
        return SNullWidget::NullWidget;
    }

private:
    FSlateColor GetStripeColor() const
    {
        return FSimpleCoreEditorWidgetUtils::GetTableRowStripeColor(IndexInList);
    }

    TSharedPtr<FWorldStateFactRow> Item;
    TAttribute<FText> HighlightText;
};

void SWorldStateFactsView::Construct(const FArguments& InArgs)
{
    // Default sort: Tag ascending. RefreshRowsFromChannel calls SortAllRows() so the initial population respects this.
    CurrentSortColumn = WorldStateFactsView_ColumnIDs::Tag;
    CurrentSortMode   = EColumnSortMode::Ascending;

    if (FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel())
    {
        SessionHistoryHandle = Channel->OnSessionHistoryChanged.AddRaw(this, &SWorldStateFactsView::HandleSessionHistoryChanged);
    }

    TSharedRef<SHeaderRow> Header = SNew(SHeaderRow)
        + SHeaderRow::Column(WorldStateFactsView_ColumnIDs::Tag)
            .DefaultLabel(LOCTEXT("ColTag", "Tag"))
            .FillWidth(0.75f)
            .SortMode_Lambda([this]() { return GetColumnSortMode(WorldStateFactsView_ColumnIDs::Tag); })
            .OnSort(this, &SWorldStateFactsView::HandleColumnSort)
        + SHeaderRow::Column(WorldStateFactsView_ColumnIDs::Count)
            .DefaultLabel(LOCTEXT("ColCount", "Count"))
            .FillWidth(0.25f)
            .HAlignHeader(HAlign_Right)
            .SortMode_Lambda([this]() { return GetColumnSortMode(WorldStateFactsView_ColumnIDs::Count); })
            .OnSort(this, &SWorldStateFactsView::HandleColumnSort);

    ListView = SNew(SListView<TSharedPtr<FWorldStateFactRow>>)
        .ListItemsSource(&Rows)
        .OnGenerateRow(this, &SWorldStateFactsView::HandleGenerateRow)
        .OnContextMenuOpening(this, &SWorldStateFactsView::HandleContextMenuOpening)
        .HeaderRow(Header)
        .SelectionMode(ESelectionMode::Multi);

    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
        [
            SNew(STextBlock)
                .Text_Lambda([this]() { return GetStatusText(); })
                .ColorAndOpacity(FSlateColor(WorldStateFactsView_Style::SubduedText))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
        [
            // Row 2: session selector (auto-width, left) + filter (fill, right). Combo always shows the effective
            // session label via Text_Lambda; user-driven picks write PinnedSessionNumber, programmatic re-syncs are
            // filtered out by the OnSelectionChanged handler.
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
            [
                SAssignNew(SessionCombo, SComboBox<TSharedPtr<int32>>)
                    .OptionsSource(&SessionItems)
                    .OnGenerateWidget(this, &SWorldStateFactsView::GenerateSessionItemWidget)
                    .OnSelectionChanged(this, &SWorldStateFactsView::HandleSessionComboSelectionChanged)
                    .IsEnabled_Lambda([this]() { return !SessionItems.IsEmpty(); })
                    [
                        SNew(STextBlock)
                            .Text_Lambda([this]() { return GetSelectedSessionLabel(); })
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [
                SNew(SSearchBox)
                    .HintText(LOCTEXT("FilterHint", "Filter by tag..."))
                    .OnTextChanged(this, &SWorldStateFactsView::HandleFilterTextChanged)
            ]
        ]
        + SVerticalBox::Slot().FillHeight(1.f)
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                // List always visible — when empty, the header row + empty body provide the dark background that the
                // overlay'd empty message sits on top of. Mirrors the Quest State view's pattern for consistency.
                ListView.ToSharedRef()
            ]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                    .Text_Lambda([this]() { return GetEmptyMessageText(); })
                    .ColorAndOpacity(FSlateColor(WorldStateFactsView_Style::SubduedText))
                    .Visibility_Lambda([this]() { return GetEmptyMessageVisibility(); })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
            ]
        ]
    ];

    HandleSessionHistoryChanged();
}

SWorldStateFactsView::~SWorldStateFactsView()
{
    if (SessionHistoryHandle.IsValid())
    {
        if (FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel())
        {
            Channel->OnSessionHistoryChanged.Remove(SessionHistoryHandle);
        }
        SessionHistoryHandle.Reset();
    }
}

TSharedRef<ITableRow> SWorldStateFactsView::HandleGenerateRow(TSharedPtr<FWorldStateFactRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SWorldStateFactRow, OwnerTable)
        .Item(Item)
        .HighlightText(this, &SWorldStateFactsView::GetFilterTextAsText);
}

TSharedPtr<SWidget> SWorldStateFactsView::HandleContextMenuOpening()
{
    if (!ListView.IsValid())
    {
        return nullptr;
    }
    const int32 NumSelected = ListView->GetNumItemsSelected();
    if (NumSelected == 0)
    {
        // No selection ⇒ no menu. Right-clicking a row auto-selects it before this fires, so an empty selection here
        // means the user right-clicked dead space below the rows.
        return nullptr;
    }

    FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/true, /*CommandList=*/nullptr);
    MenuBuilder.BeginSection(NAME_None, LOCTEXT("ClipboardSection", "Clipboard"));

    const FText CopyListLabel = NumSelected > 1
        ? FText::Format(LOCTEXT("CopyTagsLabel", "Copy {0} Tags"), FText::AsNumber(NumSelected))
        : LOCTEXT("CopyTagLabel", "Copy Tag");

    MenuBuilder.AddMenuEntry(
        CopyListLabel,
        LOCTEXT("CopyTagTooltip", "Copy the selected tag(s) to the clipboard, one per line."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(this, &SWorldStateFactsView::CopySelectionAsTagList)));

    MenuBuilder.AddMenuEntry(
        LOCTEXT("CopyAsContainerLabel", "Copy as Tag Container"),
        LOCTEXT("CopyAsContainerTooltip", "Copy as text in FGameplayTagContainer export format — paste into a Tag Container property to import."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(this, &SWorldStateFactsView::CopySelectionAsTagContainer)));

    MenuBuilder.AddMenuEntry(
        LOCTEXT("CopyRowsTSV", "Copy Row(s) as TSV"),
        LOCTEXT("CopyRowsTSVTooltip", "Copy each selected row as tab-separated values: Tag\\tCount with header row. Paste into a spreadsheet for analysis."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(this, &SWorldStateFactsView::CopySelectionAsTSV)));

    MenuBuilder.EndSection();
    return MenuBuilder.MakeWidget();
}

EVisibility SWorldStateFactsView::GetEmptyMessageVisibility() const
{
    return Rows.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SWorldStateFactsView::GetEmptyMessageText() const
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel || GetEffectiveSessionIndex() == INDEX_NONE)
    {
        return LOCTEXT("EmptyNotInPIE", "Not in PIE — start Play In Editor to inspect live WorldState facts.");
    }
    if (AllRows.IsEmpty())
    {
        return Channel->IsActive()
            ? LOCTEXT("EmptyNoFacts", "PIE active — no facts have been asserted yet.")
            : LOCTEXT("EmptyNoFactsSnapshot", "SNAPSHOT — no facts captured this session.");
    }
    return FText::Format(LOCTEXT("EmptyFilterMismatch", "No facts match filter '{0}'."), FText::FromString(FilterText));
}

FText SWorldStateFactsView::GetStatusText() const
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel) return LOCTEXT("StatusIdle", "idle");

    const FWorldStateSessionSnapshot* Session = Channel->GetSessionByIndex(GetEffectiveSessionIndex());
    if (!Session) return LOCTEXT("StatusIdle", "idle");

    const FText CountText = (AllRows.Num() != Rows.Num())
        ? FText::Format(LOCTEXT("StatusCountFiltered", "{0} fact(s), {1} shown"), FText::AsNumber(AllRows.Num()), FText::AsNumber(Rows.Num()))
        : FText::Format(LOCTEXT("StatusCount", "{0} fact(s)"), FText::AsNumber(AllRows.Num()));

    if (Session->bInFlight)
    {
        return FText::Format(LOCTEXT("StatusActive", "DEBUG (PIE) — {0}"), CountText);
    }

    FNumberFormattingOptions TimeOpts;
    TimeOpts.MinimumFractionalDigits = 2;
    TimeOpts.MaximumFractionalDigits = 2;
    return FText::Format(LOCTEXT("StatusSnapshot", "SNAPSHOT — Session {0} ended at t={1}s — {2}"),
        FText::AsNumber(Session->SessionNumber),
        FText::AsNumber(Session->EndedAtGameTime, &TimeOpts),
        CountText);
}

void SWorldStateFactsView::HandleSessionHistoryChanged()
{
    RebuildSessionItems();
    if (RefreshRowsFromChannel() && ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

int32 SWorldStateFactsView::GetEffectiveSessionIndex() const
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel) return INDEX_NONE;
    const TArray<FWorldStateSessionSnapshot>& History = Channel->GetSessionHistory();
    if (History.IsEmpty()) return INDEX_NONE;
    if (PinnedSessionNumber != INDEX_NONE)
    {
        // Walk by SessionNumber so a FIFO eviction of the pinned session falls through to "latest" cleanly rather
        // than silently shifting the pin onto a different session that happens to occupy the old array index.
        for (int32 i = 0; i < History.Num(); ++i)
        {
            if (History[i].SessionNumber == PinnedSessionNumber) return i;
        }
    }
    return History.Num() - 1;  // auto = latest, OR pinned-but-evicted fallback
}

void SWorldStateFactsView::HandleFilterTextChanged(const FText& NewText)
{
    FilterText = NewText.ToString();
    ApplyFilter();
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

void SWorldStateFactsView::ApplyFilter()
{
    Rows.Reset();
    if (FilterText.IsEmpty())
    {
        Rows.Append(AllRows);
        return;
    }
    for (const TSharedPtr<FWorldStateFactRow>& Row : AllRows)
    {
        if (Row.IsValid() && Row->Tag.GetTagName().ToString().Contains(FilterText))
        {
            Rows.Add(Row);
        }
    }
}

bool SWorldStateFactsView::RefreshRowsFromChannel()
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel) return false;

    const int32 EffectiveIndex = GetEffectiveSessionIndex();

    // No sessions yet (fresh editor session, never PIE'd). Wipe rows if not already empty so the empty state shows.
    if (EffectiveIndex == INDEX_NONE)
    {
        if (AllRows.IsEmpty()) return false;
        AllRows.Reset();
        ApplyFilter();
        return true;
    }

    const TMap<FGameplayTag, int32>& Facts = Channel->GetFactsForSession(EffectiveIndex);

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
    SortAllRows();
    ApplyFilter();
    return true;
}

void SWorldStateFactsView::SortAllRows()
{
    if (CurrentSortMode == EColumnSortMode::None)
    {
        return;
    }
    const bool bAscending = (CurrentSortMode == EColumnSortMode::Ascending);

    if (CurrentSortColumn == WorldStateFactsView_ColumnIDs::Tag)
    {
        AllRows.Sort([bAscending](const TSharedPtr<FWorldStateFactRow>& A, const TSharedPtr<FWorldStateFactRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAscending
                ? A->Tag.GetTagName().LexicalLess(B->Tag.GetTagName())
                : B->Tag.GetTagName().LexicalLess(A->Tag.GetTagName());
        });
    }
    else if (CurrentSortColumn == WorldStateFactsView_ColumnIDs::Count)
    {
        AllRows.Sort([bAscending](const TSharedPtr<FWorldStateFactRow>& A, const TSharedPtr<FWorldStateFactRow>& B)
        {
            if (!A.IsValid() || !B.IsValid()) return false;
            return bAscending ? A->Count < B->Count : A->Count > B->Count;
        });
    }
}

EColumnSortMode::Type SWorldStateFactsView::GetColumnSortMode(FName ColumnID) const
{
    return ColumnID == CurrentSortColumn ? CurrentSortMode : EColumnSortMode::None;
}

void SWorldStateFactsView::HandleColumnSort(EColumnSortPriority::Type /*SortPriority*/, const FName& ColumnID, EColumnSortMode::Type NewSortMode)
{
    CurrentSortColumn = ColumnID;
    CurrentSortMode   = NewSortMode;
    SortAllRows();
    ApplyFilter();
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

void SWorldStateFactsView::CopySelectionAsTagList()
{
    if (!ListView.IsValid()) return;

    // Iterate Rows (filtered + sorted) so the clipboard order matches the user's visible order, not the internal
    // selection-array order. IsItemSelected() checks set membership in O(log N) within SListView.
    TArray<FString> TagStrings;
    TagStrings.Reserve(ListView->GetNumItemsSelected());
    for (const TSharedPtr<FWorldStateFactRow>& Row : Rows)
    {
        if (Row.IsValid() && Row->Tag.IsValid() && ListView->IsItemSelected(Row))
        {
            TagStrings.Add(Row->Tag.GetTagName().ToString());
        }
    }
    if (TagStrings.IsEmpty()) return;

    const FString Joined = FString::Join(TagStrings, TEXT("\n"));
    FPlatformApplicationMisc::ClipboardCopy(*Joined);
}

void SWorldStateFactsView::CopySelectionAsTagContainer()
{
    if (!ListView.IsValid()) return;

    // FGameplayTagContainer's UPROPERTY text-export format. UE's property paste path on a tag-container property
    // accepts this exact shape and imports the tags directly.
    FString Out = TEXT("(GameplayTags=(");
    bool bFirst = true;
    for (const TSharedPtr<FWorldStateFactRow>& Row : Rows)
    {
        if (!Row.IsValid() || !Row->Tag.IsValid() || !ListView->IsItemSelected(Row)) continue;
        if (!bFirst) Out += TEXT(",");
        Out += FString::Printf(TEXT("(TagName=\"%s\")"), *Row->Tag.GetTagName().ToString());
        bFirst = false;
    }
    Out += TEXT("))");

    if (bFirst)
    {
        return;  // nothing was actually selected/valid
    }
    FPlatformApplicationMisc::ClipboardCopy(*Out);
}

void SWorldStateFactsView::CopySelectionAsTSV()
{
    if (!ListView.IsValid()) return;

    // Iterate Rows (filtered + sorted) so the clipboard order matches what the user sees, not the internal
    // selection-array order. Header row first; row data tab-separated. Mirrors the Quest State view's TSV format.
    TArray<FString> Lines;
    Lines.Add(TEXT("Tag\tCount"));
    for (const TSharedPtr<FWorldStateFactRow>& Row : Rows)
    {
        if (!Row.IsValid() || !Row->Tag.IsValid() || !ListView->IsItemSelected(Row)) continue;
        Lines.Add(FString::Printf(TEXT("%s\t%d"), *Row->Tag.GetTagName().ToString(), Row->Count));
    }
    if (Lines.Num() <= 1) return;  // header only, no rows selected
    FPlatformApplicationMisc::ClipboardCopy(*FString::Join(Lines, TEXT("\n")));
}

void SWorldStateFactsView::RebuildSessionItems()
{
    SessionItems.Reset();
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (Channel)
    {
        const TArray<FWorldStateSessionSnapshot>& History = Channel->GetSessionHistory();
        SessionItems.Reserve(History.Num());
        for (int32 i = 0; i < History.Num(); ++i)
        {
            SessionItems.Add(MakeShared<int32>(i));
        }
    }
    if (SessionCombo.IsValid())
    {
        SessionCombo->RefreshOptions();
        // Re-sync the combo's selected item to mirror the effective index. SetSelectedItem fires OnSelectionChanged
        // with ESelectInfo::Direct — the handler filters that out so this re-sync doesn't spuriously rewrite
        // PinnedSessionNumber.
        const int32 EffectiveIndex = GetEffectiveSessionIndex();
        if (SessionItems.IsValidIndex(EffectiveIndex))
        {
            SessionCombo->SetSelectedItem(SessionItems[EffectiveIndex]);
        }
        else
        {
            SessionCombo->ClearSelection();
        }
    }
}

TSharedRef<SWidget> SWorldStateFactsView::GenerateSessionItemWidget(TSharedPtr<int32> InItem)
{
    if (!InItem.IsValid()) return SNullWidget::NullWidget;
    const int32 Index = *InItem;
    return SNew(STextBlock)
        .Text(MakeSessionLabel(Index))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
}

void SWorldStateFactsView::HandleSessionComboSelectionChanged(TSharedPtr<int32> NewItem, ESelectInfo::Type SelectInfo)
{
    // Direct = programmatic SetSelectedItem call (e.g. RebuildSessionItems re-sync). Only user-driven picks should
    // mutate pin state.
    if (SelectInfo == ESelectInfo::Direct) return;
    if (!NewItem.IsValid()) return;

    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel) return;

    const FWorldStateSessionSnapshot* Session = Channel->GetSessionByIndex(*NewItem);
    if (!Session) return;

    // If the user picked the latest session, clear the pin so future PIE starts auto-advance to the new latest;
    // otherwise pin to the explicit SessionNumber.
    const TArray<FWorldStateSessionSnapshot>& History = Channel->GetSessionHistory();
    PinnedSessionNumber = (*NewItem == History.Num() - 1) ? INDEX_NONE : Session->SessionNumber;

    if (RefreshRowsFromChannel() && ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

FText SWorldStateFactsView::MakeSessionLabel(int32 Index) const
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel) return FText::GetEmpty();
    const FWorldStateSessionSnapshot* Session = Channel->GetSessionByIndex(Index);
    if (!Session) return FText::GetEmpty();

    if (Session->bInFlight)
    {
        return FText::Format(LOCTEXT("SessionLabelInFlight", "Session {0} (in flight)"),
            FText::AsNumber(Session->SessionNumber));
    }
    FNumberFormattingOptions TimeOpts;
    TimeOpts.MinimumFractionalDigits = 2;
    TimeOpts.MaximumFractionalDigits = 2;
    return FText::Format(LOCTEXT("SessionLabelEnded", "Session {0} (ended at t={1}s)"),
        FText::AsNumber(Session->SessionNumber),
        FText::AsNumber(Session->EndedAtGameTime, &TimeOpts));
}

FText SWorldStateFactsView::GetSelectedSessionLabel() const
{
    const int32 EffectiveIndex = GetEffectiveSessionIndex();
    if (EffectiveIndex == INDEX_NONE)
    {
        return LOCTEXT("SessionLabelNone", "(no sessions)");
    }
    return MakeSessionLabel(EffectiveIndex);
}

#undef LOCTEXT_NAMESPACE