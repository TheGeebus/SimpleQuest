// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "QuestWatcherInterface.generated.h"

class UQuest;
// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UQuestWatcherInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SIMPLEQUEST_API IQuestWatcherInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	/*
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void WatchedQuestActivatedEvent(UQuest* InWatchedQuest);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void WatchedQuestStepStartedEvent(UQuest* InWatchedQuest, const int32 InStartedStepID);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void WatchedStepCompletedEvent(UQuest* InWatchedQuest, const int32 InCompletedStepID, bool bDidSucceed);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void WatchedQuestCompletedEvent(UQuest* InWatchedQuest, bool bDidSucceed);
	*/
};
