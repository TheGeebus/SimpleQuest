// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "QuestTargetInterface.generated.h"

class UQuestTargetDelegateWrapper;

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UQuestTargetInterface : public UInterface
{
	GENERATED_BODY()
};

	/**
	 * This interface is implemented by the actor component class, UQuestTargetComponent. Consider adding the
	 * UQuestTargetComponent actor component to enable Quest related functionality and delegates rather than implementing
	 * this interface directly.
	 *
	 * See: UQuestTargetComponent::BeginPlay and UQuestTargetComponent::GetQuestDelegateWrapper_Implementation
	 */
class SIMPLEQUEST_API IQuestTargetInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void SetActivated(bool bIsActivated);

};

