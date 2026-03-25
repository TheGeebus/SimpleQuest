// Fill out your copyright notice in the Description page of Project Settings.


#include "Objectives/Examples/GoToQuestObjective.h"

#include "SimpleQuestLog.h"
#include "Interfaces/QuestTargetDelegateWrapper.h"
#include "Interfaces/QuestTargetInterface.h"


void UGoToQuestObjective::TryCompleteObjective_Implementation(UObject* InTargetObject)
{
	UE_LOG(LogSimpleQuest, Log, TEXT("UGoToQuestObjective::TryCompleteObjective_Implementation : finished objective: %s"), *GetFullName());
	EnableTargetObject(InTargetObject, false);
	CompleteObjective(true);
}

void UGoToQuestObjective::SetObjectiveTarget_Implementation(int32 InStepID, const TSet<TSoftObjectPtr<AActor>>& InTargetActors, UClass* InTargetClass,
	int32 NumElementsRequired, bool bUseCounter)
{
	Super::SetObjectiveTarget_Implementation(InStepID, InTargetActors, InTargetClass, NumElementsRequired, bUseCounter);

	EnableQuestTargetActors(true);
}
