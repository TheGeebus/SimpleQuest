// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Objectives/Examples/GoToQuestObjective.h"


UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_GoTo_Reached, "SimpleQuest.QuestOutcome.Reached")

UGoToQuestObjective::UGoToQuestObjective()
{
	ReachedOutcomeTag = Tag_Outcome_GoTo_Reached;
}

void UGoToQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveContext& InContext)
{
	EnableTargetObject(InContext.TriggeredActor, false);
	CompleteObjectiveWithOutcome(ReachedOutcomeTag, InContext);
}

void UGoToQuestObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveActivationParams& Params)
{
	Super::OnObjectiveActivated_Implementation(Params);
	EnableQuestTargetActors(true);
}
