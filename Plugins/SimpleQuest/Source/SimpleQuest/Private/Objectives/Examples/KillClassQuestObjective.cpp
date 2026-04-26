// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Objectives/Examples/KillClassQuestObjective.h"

#include "SimpleQuestLog.h"

UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_KillClass_Killed, "SimpleQuest.QuestOutcome.TargetKilled")


UKillClassQuestObjective::UKillClassQuestObjective()
{
	TargetKilledOutcomeTag = Tag_Outcome_KillClass_Killed;
}

void UKillClassQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveContext& InContext)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("KillClassQuestObjective::TryCompleteObjective checked: %s"),
		InContext.TriggeredActor ? *InContext.TriggeredActor->GetName() : TEXT("null"));

	if (AddProgress(InContext, Tag_Outcome_KillClass_Killed))
	{
		EnableTargetObject(InContext.TriggeredActor, false);
	}
}
