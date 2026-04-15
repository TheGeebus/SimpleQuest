// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Objectives/Examples/KillClassQuestObjective.h"

#include "SimpleQuestLog.h"

UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_KillClass_Killed, "Quest.Outcome.TargetKilled")

void UKillClassQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveContext& InContext)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("KillClassQuestObjective::TryCompleteObjective checked: %s"),
		InContext.TriggeredActor ? *InContext.TriggeredActor->GetName() : TEXT("null"));

	SetCurrentElements(GetCurrentElements() + 1);
	if (GetCurrentElements() >= GetMaxElements())
	{
		UE_LOG(LogSimpleQuest, Log, TEXT("KillClassQuestObjective::TryCompleteObjective completed: %s"), *GetFullName());
		EnableTargetObject(InContext.TriggeredActor, false);

		FQuestObjectiveContext OutContext = InContext;
		OutContext.CurrentCount = GetCurrentElements();
		OutContext.RequiredCount = GetMaxElements();
		CompleteObjectiveWithOutcome(Tag_Outcome_KillClass_Killed, OutContext);
		return;
	}

	UE_LOG(LogSimpleQuest, Verbose, TEXT("KillClassQuestObjective::TryCompleteObjective progress: %d/%d — %s"),
		GetCurrentElements(), GetMaxElements(), *GetFullName());
}

