// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestStep.h" 

#include "Objectives/QuestObjective.h"

void UQuestStep::Activate(FGameplayTag InContextualTag)
{
	Super::Activate(InContextualTag);

	UClass* ObjClass = QuestObjective.LoadSynchronous();
	if (!ObjClass) return;

	ActiveObjective = NewObject<UQuestObjective>(this, ObjClass);
	ActiveObjective->OnEnableTarget.AddDynamic(this, &UQuestStep::OnObjectiveEnabledEvent);
	ActiveObjective->OnQuestObjectiveComplete.AddDynamic(this, &UQuestStep::OnObjectiveComplete);
	ActiveObjective->SetObjectiveTarget(0, TargetActors, TargetClass, NumberOfElements, false);
}

void UQuestStep::OnObjectiveEnabledEvent(UObject* InTargetObject, int32 InStepID, bool bNewIsEnabled)
{
	OnStepTargetEnabled.ExecuteIfBound(this, InTargetObject, bNewIsEnabled);
}

void UQuestStep::OnObjectiveComplete(int32 StepID, bool bDidSucceed)
{
	OnNodeCompleted.ExecuteIfBound(this, bDidSucceed);
}

