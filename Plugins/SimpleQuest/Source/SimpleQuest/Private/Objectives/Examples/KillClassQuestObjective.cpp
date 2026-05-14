// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#include "Objectives/Examples/KillClassQuestObjective.h"

#include "SimpleQuestLog.h"

UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_KillClass_Killed, "SimpleQuest.Outcome.TargetKilled")


UKillClassQuestObjective::UKillClassQuestObjective()
{
	TargetKilledOutcomeTag = Tag_Outcome_KillClass_Killed;
}

void UKillClassQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& InContext)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("KillClassQuestObjective::TryCompleteObjective checked: %s"),
		InContext.TriggeredActor ? *InContext.TriggeredActor->GetName() : TEXT("null"));

	if (AddProgress(InContext, Tag_Outcome_KillClass_Killed))
	{
		EnableTargetObject(InContext.TriggeredActor, false);
	}
}
