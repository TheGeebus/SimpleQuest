// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "QuestGiverInterface.generated.h"


// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UQuestGiverInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SIMPLEQUEST_API IQuestGiverInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	virtual void SetQuestGiverActivated(const FGameplayTag& QuestTag, bool bIsQuestActive);
};
