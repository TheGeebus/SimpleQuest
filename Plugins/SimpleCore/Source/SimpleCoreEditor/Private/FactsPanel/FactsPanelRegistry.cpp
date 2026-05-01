// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "FactsPanel/FactsPanelRegistry.h"

FFactsPanelRegistry& FFactsPanelRegistry::Get()
{
	static FFactsPanelRegistry Instance;
	return Instance;
}

void FFactsPanelRegistry::RegisterView(FName ViewID, const FText& DisplayName, FFactsViewWidgetFactory Factory)
{
	if (ViewID.IsNone() || !Factory)
	{
		return;
	}

	// Replace-on-collision keeps registration idempotent across module reload.
	if (FFactsViewRegistration* Existing = RegisteredViews.FindByPredicate([ViewID](const FFactsViewRegistration& R) { return R.ViewID == ViewID; }))
	{
		Existing->DisplayName = DisplayName;
		Existing->Factory = MoveTemp(Factory);
	}
	else
	{
		RegisteredViews.Add({ ViewID, DisplayName, MoveTemp(Factory) });
	}
	OnViewsChanged.Broadcast();
}

void FFactsPanelRegistry::UnregisterView(FName ViewID)
{
	const int32 Removed = RegisteredViews.RemoveAll([ViewID](const FFactsViewRegistration& R) { return R.ViewID == ViewID; });
	if (Removed > 0)
	{
		OnViewsChanged.Broadcast();
	}
}

const FFactsViewRegistration* FFactsPanelRegistry::FindView(FName ViewID) const
{
	return RegisteredViews.FindByPredicate([ViewID](const FFactsViewRegistration& R) { return R.ViewID == ViewID; });
}