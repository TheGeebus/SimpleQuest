// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "ActivationGroupGetterNode.generated.h"

struct FWorldStateFactAddedEvent;

UCLASS()
class SIMPLEQUEST_API UActivationGroupGetterNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag GroupTag;
	
	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
	virtual void DeactivateInternal(FGameplayTag InContextualTag) override;

private:
	FDelegateHandle SignalSubscriptionHandle;

	void OnGroupSignalFired(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
};