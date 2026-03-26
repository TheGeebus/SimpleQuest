// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FQuestlineGraphAssetTypeActions;

class FSimpleQuestEditor : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<FQuestlineGraphAssetTypeActions> QuestlineGraphAssetTypeActions;
};
