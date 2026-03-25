// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "QuestGiverInterface.generated.h"

class UQuest;

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

	virtual void SetQuestGiverActivated(const TSubclassOf<UQuest>& QuestClassToEnable, const FName& QuestID, bool bIsQuestActive);
};
