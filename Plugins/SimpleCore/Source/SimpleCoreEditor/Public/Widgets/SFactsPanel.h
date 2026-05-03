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
        /** Optional: ViewId to select on construction. If unset/unknown, falls through the resolution chain
         *  below. */
        SLATE_ARGUMENT(FName, InitialViewID)

        /** Optional: stable per-instance key for persisting the active selection to GEditorPerProjectIni.
         *  Nomad tab spawners pass the tab's InstanceId here so each docked panel remembers its last view
         *  across editor sessions. Empty key disables persistence. */
        SLATE_ARGUMENT(FName, PersistenceKey)

        /** Optional: explicit fallback ViewID used when no other source resolves a registered view. Resolution
         *  priority is: persisted selection (if PersistenceKey set + saved + registered) > InitialViewID >
         *  FallbackViewID > first registered view. */
        SLATE_ARGUMENT(FName, FallbackViewID)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SFactsPanel();

    
    /**
     * Returns "Facts - <ActiveViewDisplayName>" for the docked tab's title, polled per-paint. Falls back to
     * "Facts Panel" when no view is currently active (registry empty, view unregistered while tab open, etc.).
     * Bound by tab spawners via Label_Lambda so the tab title tracks the dropdown selection live.
     */
    FText GetActiveViewLabel() const;

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
    
    /** Persistence key passed in via SLATE_ARGUMENT; empty when persistence is disabled. */
    FName PersistenceKey;
};