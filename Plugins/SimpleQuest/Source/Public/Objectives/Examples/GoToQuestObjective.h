// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Objectives/QuestObjective.h"
#include "GoToQuestObjective.generated.h"

/**
 * 
 */
UCLASS()
class SIMPLEQUEST_API UGoToQuestObjective : public UQuestObjective
{
	GENERATED_BODY()

protected:
	UFUNCTION()
	virtual void TryCompleteObjective_Implementation(UObject* InTargetObject) override;
	virtual void SetObjectiveTarget_Implementation(int32 InStepID, const TSet<TSoftObjectPtr<AActor>>& InTargetActors, UClass* InTargetClass = nullptr, int32 NumElementsRequired = 0, bool bUseCounter = false) override;
};
