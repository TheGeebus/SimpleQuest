// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Details customization for UQuestImportMapping: renders the stock properties (discriminator, class map, source descriptor,
// policies) as normal rows, and embeds the bespoke binding-list widget for the column->property bindings. Follows the module's
// existing customization pattern (see QuestlineGraphRewardsDetailsCustomization).

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class SQuestMappingBindingList;
class UQuestImportMapping;

class FQuestImportMappingDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<UQuestImportMapping> Mapping;
	TSharedPtr<SQuestMappingBindingList> BindingList;

	TArray<FName> EnumerateSourceColumnsForMapping() const;   // the provider the binding list calls
	void OnMappingModified();                                  // mark dirty + refresh readiness (later)
};