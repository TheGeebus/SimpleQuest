// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Objectives/Examples/KillClassQuestObjective.h"

#include "SimpleQuestLog.h"

UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_KillClass_Killed, "Quest.Outcome.TargetKilled")

void UKillClassQuestObjective::TryCompleteObjective_Implementation(UObject* InTargetObject)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UKillClassQuestObjective::TryCompleteObjective_Implementation checked: %s"), *InTargetObject->GetName());
	SetCurrentElements(GetCurrentElements() + 1);
	if (GetCurrentElements() >= GetMaxElements())
	{
		UE_LOG(LogSimpleQuest, Log, TEXT("UKillClassQuestObjective::TryCompleteObjective_Implementation completed objective: %s"), *GetFullName());
		EnableTargetObject(InTargetObject, false);
		CompleteObjectiveWithOutcome(Tag_Outcome_KillClass_Killed);
		return;
	}
	UE_LOG(LogSimpleQuest, Verbose, TEXT("UKillClassQuestObjective::TryCompleteObjective_Implementation did not complete: %s"), *GetFullName());
}
