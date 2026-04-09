// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestStep.h" 

#include "Objectives/QuestObjective.h"

void UQuestStep::ActivateInternal(FGameplayTag InContextualTag)
{
	Super::ActivateInternal(InContextualTag);

	UClass* ObjClass = QuestObjective.LoadSynchronous();
	if (!ObjClass) return;

	ActiveObjective = NewObject<UQuestObjective>(this, ObjClass);
	ActiveObjective->OnEnableTarget.AddDynamic(this, &UQuestStep::OnObjectiveEnabledEvent);
	ActiveObjective->OnQuestObjectiveComplete.AddDynamic(this, &UQuestStep::OnObjectiveComplete);
	ActiveObjective->SetObjectiveTarget(TargetActors, TargetClass, NumberOfElements, false);
}

void UQuestStep::OnObjectiveEnabledEvent(UObject* InTargetObject, bool bNewIsEnabled)
{
	OnStepTargetEnabled.ExecuteIfBound(this, InTargetObject, bNewIsEnabled);
}

void UQuestStep::OnObjectiveComplete(FGameplayTag OutcomeTag)
{
	OnNodeCompleted.ExecuteIfBound(this, OutcomeTag);
}

