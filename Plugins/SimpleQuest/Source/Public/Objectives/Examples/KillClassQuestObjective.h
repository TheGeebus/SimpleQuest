// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Objectives/QuestObjective.h"
#include "KillClassQuestObjective.generated.h"

/**
 * 
 */
UCLASS()
class SIMPLEQUEST_API UKillClassQuestObjective : public UQuestObjective
{
	GENERATED_BODY()

public:
	virtual void TryCompleteObjective_Implementation(UObject* InTargetObject) override;

};
