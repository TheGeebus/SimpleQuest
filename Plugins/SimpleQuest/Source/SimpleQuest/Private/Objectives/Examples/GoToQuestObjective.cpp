// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Objectives/Examples/GoToQuestObjective.h"


UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_GoTo_Reached, "Quest.Outcome.Reached")

UGoToQuestObjective::UGoToQuestObjective()
{
	ReachedOutcomeTag = Tag_Outcome_GoTo_Reached;
}

void UGoToQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveContext& InContext)
{
	EnableTargetObject(InContext.TriggeredActor, false);
	CompleteObjectiveWithOutcome(ReachedOutcomeTag, InContext);
}

void UGoToQuestObjective::SetObjectiveTarget_Implementation(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, const TSet<TSubclassOf<AActor>>& InTargetClasses, int32 NumElementsRequired)
{
	Super::SetObjectiveTarget_Implementation(InTargetActors, InTargetClasses, NumElementsRequired);
	EnableQuestTargetActors(true);
}
