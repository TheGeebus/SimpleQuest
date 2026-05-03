// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SFactsPanel.h"

#include "FactsPanel/FactsPanelRegistry.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "SFactsPanel"

namespace FactsPanel_Style
{
    const FLinearColor SubduedText = FLinearColor(0.55f, 0.55f, 0.55f);
}

void SFactsPanel::Construct(const FArguments& InArgs)
{
    PersistenceKey = InArgs._PersistenceKey;

    RebuildViewOptions();

    // Resolve initial selection in priority order:
    //   1. Persisted last selection (GConfig under PersistenceKey, if registered)
    //   2. InArgs._InitialViewID (if registered)
    //   3. InArgs._FallbackViewID (if registered) — typically WorldState, set by the spawner
    //   4. First registered view (ViewOptions[0]) — last-resort tiebreaker
    auto TryResolve = [this](const FName& ViewId) -> bool
    {
        if (ViewId.IsNone()) return false;
        for (const TSharedPtr<FName>& Option : ViewOptions)
        {
            if (Option.IsValid() && *Option == ViewId)
            {
                ActiveOption = Option;
                return true;
            }
        }
        return false;
    };

    if (!PersistenceKey.IsNone())
    {
        FString SavedViewID;
        GConfig->GetString(TEXT("FactsPanel"),
            *FString::Printf(TEXT("%s.LastView"), *PersistenceKey.ToString()),
            SavedViewID, GEditorPerProjectIni);
        if (!SavedViewID.IsEmpty())
        {
            TryResolve(FName(*SavedViewID));
        }
    }
    if (!ActiveOption.IsValid()) { TryResolve(InArgs._InitialViewID); }
    if (!ActiveOption.IsValid()) { TryResolve(InArgs._FallbackViewID); }
    if (!ActiveOption.IsValid() && ViewOptions.Num() > 0)
    {
        ActiveOption = ViewOptions[0];
    }

    ViewsChangedHandle = FFactsPanelRegistry::Get().OnViewsChanged.AddRaw(this, &SFactsPanel::HandleViewsChanged);

    ContentHost = SNew(SBox);

    ChildSlot
    [
        SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(4.f))
            [
                SNew(SVerticalBox)
                // Dropdown row — switches the hosted view. No fixed title; the dropdown's current selection is the title.
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
                    [
                        SNew(STextBlock)
                            .Text(LOCTEXT("ViewLabel", "View:"))
                            .ColorAndOpacity(FSlateColor(FactsPanel_Style::SubduedText))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SAssignNew(ComboBox, SComboBox<TSharedPtr<FName>>)
                            .OptionsSource(&ViewOptions)
                            .OnSelectionChanged(this, &SFactsPanel::HandleSelectionChanged)
                            .OnGenerateWidget(this, &SFactsPanel::HandleGenerateOption)
                            .InitiallySelectedItem(ActiveOption)
                            [
                                SNew(STextBlock)
                                    .Text_Lambda([this]() { return GetCurrentSelectionText(); })
                                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                            ]
                    ]
                ]
                // Hosted view — owns its own layout, refresh, filtering, etc. The empty overlay only shows when no
                // views are registered (e.g., SimpleQuestEditor unloaded and we've already lost SimpleCore's view).
                + SVerticalBox::Slot().FillHeight(1.f)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        ContentHost.ToSharedRef()
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                            .Text_Lambda([this]() { return GetEmptyHostMessage(); })
                            .ColorAndOpacity(FSlateColor(FactsPanel_Style::SubduedText))
                            .Visibility_Lambda([this]() { return GetEmptyHostVisibility(); })
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    ]
                ]
            ]
    ];

    if (ActiveOption.IsValid())
    {
        SwitchToView(*ActiveOption);
    }
}

SFactsPanel::~SFactsPanel()
{
    if (ViewsChangedHandle.IsValid())
    {
        FFactsPanelRegistry::Get().OnViewsChanged.Remove(ViewsChangedHandle);
        ViewsChangedHandle.Reset();
    }
}

FText SFactsPanel::GetActiveViewLabel() const
{
    if (!ActiveOption.IsValid())
    {
        return LOCTEXT("FactsPanelDefaultLabel", "Facts Panel");
    }
    const FFactsViewRegistration* Reg = FFactsPanelRegistry::Get().FindView(*ActiveOption);
    if (!Reg)
    {
        return LOCTEXT("FactsPanelDefaultLabel", "Facts Panel");
    }
    return FText::Format(LOCTEXT("FactsPanelLabelFmt", "Facts: {0}"), Reg->DisplayName);
}

void SFactsPanel::RebuildViewOptions()
{
    const TArray<FFactsViewRegistration>& Registrations = FFactsPanelRegistry::Get().GetRegisteredViews();
    ViewOptions.Reset(Registrations.Num());
    for (const FFactsViewRegistration& R : Registrations)
    {
        ViewOptions.Add(MakeShared<FName>(R.ViewID));
    }
}

void SFactsPanel::SwitchToView(FName ViewId)
{
    if (!ContentHost.IsValid())
    {
        return;
    }
    const FFactsViewRegistration* Reg = FFactsPanelRegistry::Get().FindView(ViewId);
    if (Reg && Reg->Factory)
    {
        ContentHost->SetContent(Reg->Factory(PersistenceKey));
    }
    else
    {
        ContentHost->SetContent(SNullWidget::NullWidget);
    }
}

void SFactsPanel::HandleViewsChanged()
{
    // Preserve the previously-selected ViewId across rebuilds so a late-arriving registration doesn't reset the user's
    // current selection. Pointers are fresh after RebuildViewOptions, so SetSelectedItem will fire the change handler
    // when ActiveOption gets reassigned — that handler does the actual SwitchToView.
    const FName PreviousId = ActiveOption.IsValid() ? *ActiveOption : NAME_None;

    RebuildViewOptions();

    TSharedPtr<FName> NewActive;
    for (const TSharedPtr<FName>& Option : ViewOptions)
    {
        if (Option.IsValid() && *Option == PreviousId)
        {
            NewActive = Option;
            break;
        }
    }
    if (!NewActive.IsValid() && ViewOptions.Num() > 0)
    {
        NewActive = ViewOptions[0];
    }
    ActiveOption = NewActive;

    if (ComboBox.IsValid())
    {
        ComboBox->RefreshOptions();
        ComboBox->SetSelectedItem(ActiveOption);
    }

    // Clearing case: registry emptied entirely. SetSelectedItem(null) doesn't reliably fire the handler, so wipe the
    // host directly.
    if (!ActiveOption.IsValid() && ContentHost.IsValid())
    {
        ContentHost->SetContent(SNullWidget::NullWidget);
    }
}

void SFactsPanel::HandleSelectionChanged(TSharedPtr<FName> NewSelection, ESelectInfo::Type /*SelectInfo*/)
{
    if (!NewSelection.IsValid())
    {
        return;
    }
    ActiveOption = NewSelection;
    SwitchToView(*NewSelection);

    // Persist the user's choice so the next construction (e.g., editor restart) restores it. Only writes
    // when PersistenceKey is set — menu-spawned panels with no key fall back to FallbackViewID each time.
    if (!PersistenceKey.IsNone())
    {
        GConfig->SetString(TEXT("FactsPanel"),
            *FString::Printf(TEXT("%s.LastView"), *PersistenceKey.ToString()),
            *NewSelection->ToString(), GEditorPerProjectIni);
        GConfig->Flush(false, GEditorPerProjectIni);
    }
}

TSharedRef<SWidget> SFactsPanel::HandleGenerateOption(TSharedPtr<FName> Option)
{
    const FFactsViewRegistration* Reg = FindRegistrationFor(Option);
    return SNew(STextBlock)
        .Text(Reg ? Reg->DisplayName : FText::GetEmpty())
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
}

FText SFactsPanel::GetCurrentSelectionText() const
{
    const FFactsViewRegistration* Reg = FindRegistrationFor(ActiveOption);
    return Reg ? Reg->DisplayName : LOCTEXT("NoViewSelected", "(no view)");
}

FText SFactsPanel::GetEmptyHostMessage() const
{
    if (ViewOptions.IsEmpty())
    {
        return LOCTEXT("NoViewsRegistered", "No facts views are registered.");
    }
    return FText::GetEmpty();
}

EVisibility SFactsPanel::GetEmptyHostVisibility() const
{
    return ViewOptions.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}

const FFactsViewRegistration* SFactsPanel::FindRegistrationFor(const TSharedPtr<FName>& Option) const
{
    return Option.IsValid() ? FFactsPanelRegistry::Get().FindView(*Option) : nullptr;
}

#undef LOCTEXT_NAMESPACE