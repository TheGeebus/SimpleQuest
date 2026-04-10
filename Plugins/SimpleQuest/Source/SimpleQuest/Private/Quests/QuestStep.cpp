// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestStep.h"
#include "Objectives/QuestObjective.h"

void UQuestStep::ActivateInternal(FGameplayTag InContextualTag)
{
	Super::ActivateInternal(InContextualTag);

	UClass* ObjClass = QuestObjective.LoadSynchronous();
	if (!ObjClass) return;

	ActiveObjective = NewObject<UQuestObjective>(this, ObjClass);
	ActiveObjective->OnQuestObjectiveComplete.AddDynamic(this, &UQuestStep::OnObjectiveComplete);
	ActiveObjective->SetObjectiveTarget(TargetActors, TargetClass, NumberOfElements, false);
}

void UQuestStep::DeactivateInternal(FGameplayTag InContextualTag)
{
	// Tear down the live objective before the base class clears deferred prereq subscriptions
	if (ActiveObjective)
	{
		ActiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		ActiveObjective = nullptr;
	}

	Super::DeactivateInternal(InContextualTag);
}

void UQuestStep::OnObjectiveComplete(FGameplayTag OutcomeTag)
{
	OnNodeCompleted.ExecuteIfBound(this, OutcomeTag);
}
