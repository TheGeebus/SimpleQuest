// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "QuestEventListenerInterface.generated.h"

struct FInstancedStruct;

// This class does not need to be modified.
UINTERFACE(Blueprintable)
class UQuestEventListenerInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SIMPLEQUEST_API IQuestEventListenerInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category="Quest Events")
	void OnQuestEventReceived(const FInstancedStruct& Event);
};
