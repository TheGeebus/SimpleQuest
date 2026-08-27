// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Factories/QuestDataAssetFactories.h"

#include "Resolver/QuestImportMapping.h"
#include "Rewards/QuestLootTable.h"
#include "Rewards/RewardSetDataAsset.h"

UQuestDataAssetFactoryBase::UQuestDataAssetFactoryBase()
{
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UQuestDataAssetFactoryBase::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	// InClass rather than SupportedClass: the picker may hand down a subclass, and honouring it is what lets an
	// adopter's derived type be created through the same entry.
	return NewObject<UObject>(InParent, InClass, InName, Flags);
}

URewardSetDataAssetFactory::URewardSetDataAssetFactory()   { SupportedClass = URewardSetDataAsset::StaticClass(); }
UQuestLootTableFactory::UQuestLootTableFactory()           { SupportedClass = UQuestLootTable::StaticClass(); }
UQuestImportMappingFactory::UQuestImportMappingFactory()   { SupportedClass = UQuestImportMapping::StaticClass(); }

