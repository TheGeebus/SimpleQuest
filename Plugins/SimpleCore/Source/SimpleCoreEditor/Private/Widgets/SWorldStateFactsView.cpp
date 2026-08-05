// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Widgets/SWorldStateFactsView.h"

#include "Debug/SimpleCorePIEDebugChannel.h"
#include "SimpleCoreEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

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


void SWorldStateFactsView::Construct(const FArguments& InArgs)
{
    if (FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel())
    {
        SessionHistoryHandle = Channel->OnSessionHistoryChanged.AddRaw(this, &SWorldStateFactsView::HandleSessionHistoryChanged);
    }

    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f ,4.f))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                    .Text_Lambda([this]() { return GetStatusText(); })
                    .ColorAndOpacity(FSlateColor(WorldStateFactsView_Style::SubduedText))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
            [
                // Height-matched invisible placeholder. Forces row 1 to allocate the same vertical space as the
                // Quest State view's segmented control so row 2 (and thus the SSearchBox hint text) lands on
                // identical sub-pixel y in both views. Width auto-collapses to zero since SBox is content-empty.
                SNew(SBox)
                    .HeightOverride(25.f)  // matches QuestState SSegmentedControl actual rendered height - creates pixel perfect swap effect
                    .Visibility(EVisibility::Hidden)
            ]
        ]
        + SVerticalBox::Slot().FillHeight(1.f)
        [
            SAssignNew(Table, SColumnTableView<TSharedPtr<FWorldStateFactRow>>)
                .Columns(MakeColumns())
                .SelectionMode(ESelectionMode::Multi)
                .PersistenceKey(InArgs._PanelPersistenceKey.IsNone()
                    ? FString()
                    : FString::Printf(TEXT("WorldStateFacts.%s"), *InArgs._PanelPersistenceKey.ToString()))
                .FilterHintText(LOCTEXT("FilterHint", "Filter by tag..."))
                .OnContextMenuOpening(this, &SWorldStateFactsView::HandleContextMenuOpening)
                .Toolbar()
                [
                    // Session selector leads the row; the table's own search fills the remainder. Combo always shows the
                    // effective session label via Text_Lambda; user-driven picks write PinnedSessionNumber, programmatic
                    // re-syncs are filtered out by the OnSelectionChanged handler.
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
                .EmptyState()
                [
                    SNew(STextBlock)
                        .Text_Lambda([this]() { return GetEmptyMessageText(); })
                        .ColorAndOpacity(FSlateColor(WorldStateFactsView_Style::SubduedText))
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

TArray<FTableColumnDef<TSharedPtr<FWorldStateFactRow>>> SWorldStateFactsView::MakeColumns()
{
    using FRowPtr = TSharedPtr<FWorldStateFactRow>;
    TArray<FTableColumnDef<FRowPtr>> Cols;

    FTableColumnDef<FRowPtr> TagCol;
    TagCol.Id        = WorldStateFactsView_ColumnIDs::Tag;
    TagCol.Label     = LOCTEXT("ColTag", "Tag");
    TagCol.FillWidth = 0.75f;
    TagCol.GetText   = [](const FRowPtr& Row) { return FText::FromName(Row->Tag.GetTagName()); };
    Cols.Add(MoveTemp(TagCol));

    FTableColumnDef<FRowPtr> CountCol;
    CountCol.Id              = WorldStateFactsView_ColumnIDs::Count;
    CountCol.Label           = LOCTEXT("ColCount", "Count");
    CountCol.FillWidth       = 0.25f;
    CountCol.HeaderAlignment = HAlign_Right;
    CountCol.CellAlignment   = HAlign_Right;
    CountCol.GetText         = [](const FRowPtr& Row) { return FText::AsNumber(Row->Count); };
    // Counts must not sort as text, or 10 lands between 1 and 2.
    CountCol.Less            = [](const FRowPtr& A, const FRowPtr& B) { return A->Count < B->Count; };
    Cols.Add(MoveTemp(CountCol));

    return Cols;
}

TSharedPtr<SWidget> SWorldStateFactsView::HandleContextMenuOpening()
{
    if (!Table.IsValid())
    {
        return nullptr;
    }
    const int32 NumSelected = Table->GetSelectedItems().Num();
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

FText SWorldStateFactsView::GetEmptyMessageText() const
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel || GetEffectiveSessionIndex() == INDEX_NONE)
    {
        return LOCTEXT("EmptyNotInPIE", "Not in PIE — start Play In Editor to inspect live WorldState facts.");
    }
    if (Rows.IsEmpty())
    {
        return Channel->IsActive()
            ? LOCTEXT("EmptyNoFacts", "PIE active — no facts have been asserted yet.")
            : LOCTEXT("EmptyNoFactsSnapshot", "SNAPSHOT — no facts captured this session.");
    }
    return FText::Format(LOCTEXT("EmptyFilterMismatch", "No facts match filter '{0}'."),
        Table.IsValid() ? Table->GetFilterText() : FText::GetEmpty());
}

FText SWorldStateFactsView::GetStatusText() const
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel) return LOCTEXT("StatusIdle", "idle");

    const FWorldStateSessionSnapshot* Session = Channel->GetSessionByIndex(GetEffectiveSessionIndex());
    if (!Session) return LOCTEXT("StatusIdle", "idle");

    const int32 VisibleCount = Table.IsValid() ? Table->GetVisibleRowCount() : Rows.Num();
    const FText CountText = (Rows.Num() != VisibleCount)
        ? FText::Format(LOCTEXT("StatusCountFiltered", "{0} fact(s), {1} shown"), FText::AsNumber(Rows.Num()), FText::AsNumber(VisibleCount))
        : FText::Format(LOCTEXT("StatusCount", "{0} fact(s)"), FText::AsNumber(Rows.Num()));

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
    // RefreshRowsFromChannel hands the new rows to the table, which re-filters, re-sorts and repaints itself.
    RefreshRowsFromChannel();
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

bool SWorldStateFactsView::RefreshRowsFromChannel()
{
    FSimpleCorePIEDebugChannel* Channel = FSimpleCoreEditorModule::GetPIEDebugChannel();
    if (!Channel) return false;

    const int32 EffectiveIndex = GetEffectiveSessionIndex();

    // No sessions yet (fresh editor session, never PIE'd). Wipe rows if not already empty so the empty state shows.
    if (EffectiveIndex == INDEX_NONE)
    {
        if (Rows.IsEmpty()) return false;
        Rows.Reset();
        if (Table.IsValid()) { Table->SetRootItems(Rows); }
        return true;
    }

    const TMap<FGameplayTag, int32>& Facts = Channel->GetFactsForSession(EffectiveIndex);

    // Cheap diff first: same count and every tag still at the same value means nothing to rebuild. Avoids replacing the
    // row array on every tick while PIE is idle, which would discard selection and scroll position for no reason.
    if (Facts.Num() == Rows.Num())
    {
        bool bAllMatch = true;
        for (const TSharedPtr<FWorldStateFactRow>& Row : Rows)
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

    Rows.Reset(Facts.Num());
    for (const TPair<FGameplayTag, int32>& Pair : Facts)
    {
        Rows.Add(MakeShared<FWorldStateFactRow>(Pair.Key, Pair.Value));
    }
    if (Table.IsValid())
    {
        Table->SetRootItems(Rows);
    }
    return true;
}

void SWorldStateFactsView::CopySelectionAsTagList()
{
    if (!Table.IsValid()) return;

    // Visible order, not selection order, so the clipboard matches what the user is looking at.
    TArray<FString> TagStrings;
    for (const TSharedPtr<FWorldStateFactRow>& Row : Table->GetSelectedItemsInVisibleOrder())
    {
        if (Row.IsValid() && Row->Tag.IsValid())
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
    if (!Table.IsValid()) return;

    // FGameplayTagContainer's UPROPERTY text-export format. UE's property paste path on a tag-container property
    // accepts this exact shape and imports the tags directly.
    FString Out = TEXT("(GameplayTags=(");
    bool bFirst = true;
    for (const TSharedPtr<FWorldStateFactRow>& Row : Table->GetSelectedItemsInVisibleOrder())
    {
        if (!Row.IsValid() || !Row->Tag.IsValid()) continue;
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
    if (!Table.IsValid()) return;
    FPlatformApplicationMisc::ClipboardCopy(*Table->GetRowsAsText(Table->GetSelectedItemsInVisibleOrder()));
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

    // RefreshRowsFromChannel hands the new rows to the table, which re-filters, re-sorts and repaints itself.
    RefreshRowsFromChannel();
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

