// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SWidget;

/** Factory signature: produce a fresh widget instance for a registered view. Called every time an SFactsPanel
 *  selects this view. The PanelPersistenceKey arg is forwarded from the hosting SFactsPanel so views can persist
 *  per-panel internal state (active sub-tab, scroll position, etc.) alongside the panel's view-selection state.
 *  Views that don't need persistence ignore the arg. */
using FFactsViewWidgetFactory = TFunction<TSharedRef<SWidget>(FName PanelPersistenceKey)>;

/** One entry in the registry - display name and factory. ViewId is the stable lookup key. */
struct FFactsViewRegistration
{
    FName                    ViewID;
    FText                    DisplayName;
    FFactsViewWidgetFactory  Factory;
};

/**
 * Registry of view factories that populate the dropdown in SFactsPanel. Each module that wants to expose live state to
 * the facts panel registers its view at module startup; SFactsPanel discovers registrations via this singleton.
 *
 * Multi-instance friendly: each SFactsPanel calls the factory to get its own fresh widget, so per-instance view state
 * (filter text, scroll position, etc.) lives on the widget rather than shared registry state. The registry itself is
 * structure-free: it imposes no presentation contract on registered views, leaving each module free to host whatever
 * widget tree best suits its data shape (table, tree, chart, custom layout).
 */
class SIMPLECOREEDITOR_API FFactsPanelRegistry
{
public:
    static FFactsPanelRegistry& Get();

    /** Register a view. ViewID must be unique; later registrations with the same ID replace earlier ones. */
    void RegisterView(FName ViewID, const FText& DisplayName, FFactsViewWidgetFactory Factory);

    /** Remove a view by ViewID. Safe to call on unknown IDs. */
    void UnregisterView(FName ViewID);

    /** All currently registered views, in registration order. */
    const TArray<FFactsViewRegistration>& GetRegisteredViews() const { return RegisteredViews; }

    /** Lookup by ViewID. Returns nullptr if unknown. */
    const FFactsViewRegistration* FindView(FName ViewID) const;

    /** Broadcast on register/unregister. Open SFactsPanel instances rebuild their dropdowns when this fires. */
    FSimpleMulticastDelegate OnViewsChanged;

private:
    TArray<FFactsViewRegistration> RegisteredViews;
};