// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "ActivationGroupSetterNode.generated.h"

UCLASS()
class SIMPLEQUEST_API UActivationGroupSetterNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag GroupTag;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};