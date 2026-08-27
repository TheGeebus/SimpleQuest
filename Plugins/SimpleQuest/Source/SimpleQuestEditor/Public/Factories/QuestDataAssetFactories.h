// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "QuestDataAssetFactories.generated.h"

/**
 * Shared behaviour for SimpleQuest's plain data assets: create one instance of SupportedClass and show up in the
 * right-click menu. Each concrete factory below exists only to name its class, because the create menu enumerates
 * factory CLASSES - one parameterized factory would produce one menu entry, not four.
 *
 * Only CONCRETE, directly-useful types get a factory. UQuestDisplayData and UQuestObjectiveConfig are meant to be
 * subclassed before they carry anything, so creating a bare one would produce an empty asset; those get browser
 * identity through FQuestAssetTypeActions and are instantiated from an adopter's own subclass.
 */
UCLASS(Abstract)
class UQuestDataAssetFactoryBase : public UFactory
{
	GENERATED_BODY()

public:
	UQuestDataAssetFactoryBase();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};

UCLASS()
class URewardSetDataAssetFactory : public UQuestDataAssetFactoryBase
{
	GENERATED_BODY()
public:
	URewardSetDataAssetFactory();
};

UCLASS()
class UQuestLootTableFactory : public UQuestDataAssetFactoryBase
{
	GENERATED_BODY()
public:
	UQuestLootTableFactory();
};

UCLASS()
class UQuestImportMappingFactory : public UQuestDataAssetFactoryBase
{
	GENERATED_BODY()
public:
	UQuestImportMappingFactory();
};

