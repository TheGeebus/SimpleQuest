// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestStep.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "SimpleQuestLog.h"
#include "Objectives/QuestObjective.h"
#include "WorldState/WorldStateSubsystem.h"

void UQuestStep::Activate(FGameplayTag InContextualTag)
{
	if (IsGiverGated())
	{
		// Giver semantics: prerequisites gate activation, same as base class.
		Super::Activate(InContextualTag);
		return;
	}

	// No giver: activate immediately and let prerequisites gate progression or completion according to PrerequisiteGateMode.
	ActivateInternal(InContextualTag);
}

void UQuestStep::ActivateInternal(FGameplayTag InContextualTag)
{
	Super::ActivateInternal(InContextualTag);

	UClass* ObjClass = QuestObjective.LoadSynchronous();
	if (!ObjClass) return;

	ActiveObjective = NewObject<UQuestObjective>(this, ObjClass);
	ActiveObjective->OnQuestObjectiveComplete.AddDynamic(this, &UQuestStep::OnObjectiveComplete);
	ActiveObjective->SetObjectiveTarget(TargetActors, TargetClasses, NumberOfElements);
}

void UQuestStep::DeactivateInternal(FGameplayTag InContextualTag)
{
	if (ActiveObjective)
	{
		ActiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		ActiveObjective = nullptr;
	}

	Super::DeactivateInternal(InContextualTag);
}

void UQuestStep::OnObjectiveComplete(FGameplayTag OutcomeTag)
{
	if (ActiveObjective)
	{
		CompletionData = ActiveObjective->TakeCompletionData();
		ActiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		ActiveObjective = nullptr;
	}
	OnNodeCompleted.ExecuteIfBound(this, OutcomeTag);
}

