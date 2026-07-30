// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "DetailCustomizations/QuestImportMappingDetailsCustomization.h"

#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/SQuestMappingBindingList.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "QuestImportMappingDetails"

TSharedRef<IDetailCustomization> FQuestImportMappingDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FQuestImportMappingDetailsCustomization);
}

TArray<FName> FQuestImportMappingDetailsCustomization::EnumerateSourceColumnsForMapping() const
{
	if (const UQuestImportMapping* M = Mapping.Get())
	{
		const FQuestSourceColumns Cols = EnumerateMappingSourceColumns(*M);
		return Cols.bReadable ? Cols.Columns : TArray<FName>();   // unreadable source -> empty list -> combos disabled/empty
	}
	return {};
}

void FQuestImportMappingDetailsCustomization::OnMappingModified()
{
	if (UQuestImportMapping* M = Mapping.Get())
	{
		M->MarkPackageDirty();
	}
	// (Readiness banner re-validation lands with the next cut; nothing else to do now.)
}

void FQuestImportMappingDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1) return;   // single-object edit only
	Mapping = Cast<UQuestImportMapping>(Objects[0].Get());
	if (!Mapping.IsValid()) return;

	// The stock properties (discriminator column, class map, source descriptor, policies, bDeleteOrphanedNodes) render as
	// normal rows in their own categories automatically — nothing to do for those. Add ONE custom row: the binding list.
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Bindings"), LOCTEXT("BindingsCategory", "Column Bindings"), ECategoryPriority::Important);

	FQuestMappingBindingListConfig Config;
	Config.Mapping = Mapping;
	Config.SourceColumnProvider = [this]() { return EnumerateSourceColumnsForMapping(); };
	Config.OnMappingModified = FSimpleDelegate::CreateSP(this, &FQuestImportMappingDetailsCustomization::OnMappingModified);

	Category.AddCustomRow(LOCTEXT("BindingListFilter", "Column Bindings"))
	[
		SAssignNew(BindingList, SQuestMappingBindingList).Config(Config)
	];
}

#undef LOCTEXT_NAMESPACE