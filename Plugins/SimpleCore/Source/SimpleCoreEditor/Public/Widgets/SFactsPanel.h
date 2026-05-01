// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"  // for ESelectInfo enum used in delegates

class SBox;
template<typename> class SComboBox;
struct FFactsViewRegistration;

/**
 * Generic facts panel shell. Provides the multi-instance frame (dropdown + content host) for any view registered via
 * FFactsPanelRegistry. The hosted widget owns its own layout, refresh policy, filtering, etc. The shell is
 * deliberately structure-free so different registries can use whatever presentation suits their data.
 *
 * Per-instance state (selected view, hosted widget) lives on this widget. Multiple SFactsPanel instances can be docked
 * side-by-side showing different registries.
 */
class SFactsPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFactsPanel) {}
        /** Optional: ViewId to select on construction. If unset/unknown, defaults to first registered view. */
        SLATE_ARGUMENT(FName, InitialViewID)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SFactsPanel();

private:
    void RebuildViewOptions();
    void SwitchToView(FName ViewId);

    void HandleViewsChanged();
    void HandleSelectionChanged(TSharedPtr<FName> NewSelection, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget> HandleGenerateOption(TSharedPtr<FName> Option);
    FText GetCurrentSelectionText() const;
    FText GetEmptyHostMessage() const;
    EVisibility GetEmptyHostVisibility() const;

    const FFactsViewRegistration* FindRegistrationFor(const TSharedPtr<FName>& Option) const;

    /** Mirror of registry view IDs, in registration order. SComboBox needs TSharedPtr items. */
    TArray<TSharedPtr<FName>> ViewOptions;

    /** Currently-selected dropdown entry. */
    TSharedPtr<FName> ActiveOption;

    /** ContentSlot holding the hosted view widget, replaced on selection change. */
    TSharedPtr<SBox> ContentHost;

    /** Combo box reference, kept so we can refresh options and reselect after registry rebuilds. */
    TSharedPtr<SComboBox<TSharedPtr<FName>>> ComboBox;

    FDelegateHandle ViewsChangedHandle;
};