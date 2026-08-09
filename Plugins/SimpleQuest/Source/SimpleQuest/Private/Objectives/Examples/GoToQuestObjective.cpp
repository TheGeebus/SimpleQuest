// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Objectives/Examples/GoToQuestObjective.h"

#include "SimpleQuestLog.h"


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

void UGoToQuestObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveAuthoredConfig& Authored, const FQuestObjectiveRuntimeContext& Runtime)
{
	Super::OnObjectiveActivated_Implementation(Authored, Runtime);
	EnableQuestTargetActors(true);
}

