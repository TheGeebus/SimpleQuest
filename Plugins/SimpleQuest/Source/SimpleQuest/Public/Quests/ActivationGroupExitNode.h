// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "ActivationGroupExitNode.generated.h"

UCLASS()
class SIMPLEQUEST_API UActivationGroupExitNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag GroupTag;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};