// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Objectives/Examples/GoToQuestObjective.h"


UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_GoTo_Reached, "SimpleQuest.Outcome.Reached")

UGoToQuestObjective::UGoToQuestObjective()
{
	ReachedOutcomeTag = Tag_Outcome_GoTo_Reached;
}

void UGoToQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& InContext)
{
	EnableTargetObject(InContext.TriggeredActor, false);
	CompleteObjectiveWithOutcome(ReachedOutcomeTag, NAME_None, InContext);
}

void UGoToQuestObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveActivationContext& Params)
{
	Super::OnObjectiveActivated_Implementation(Params);
	EnableQuestTargetActors(true);
}
