// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Objectives/Examples/GoToQuestObjective.h"

#include "SimpleQuestLog.h"
#include "Interfaces/QuestTargetDelegateWrapper.h"
#include "Interfaces/QuestTargetInterface.h"

UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_GoTo_Reached, "Quest.BuiltIn.GoTo.Outcome.Reached")

void UGoToQuestObjective::TryCompleteObjective_Implementation(UObject* InTargetObject)
{
	EnableTargetObject(InTargetObject, false);
	CompleteObjectiveWithOutcome(Tag_Outcome_GoTo_Reached);
}

void UGoToQuestObjective::SetObjectiveTarget_Implementation(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, const TSet<TSubclassOf<AActor>>& InTargetClasses, int32 NumElementsRequired, bool bUseCounter)
{
	Super::SetObjectiveTarget_Implementation(InTargetActors, InTargetClasses, NumElementsRequired, bUseCounter);
	EnableQuestTargetActors(true);
}
