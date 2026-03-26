// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "QuestlineGraphFactory.generated.h"

UCLASS()
class UQuestlineGraphFactory : public UFactory
{
	GENERATED_BODY()

public:
	UQuestlineGraphFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};
