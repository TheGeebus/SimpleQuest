// Fill out your copyright notice in the Description page of Project Settings.


#include "Objectives/Examples/KillClassQuestObjective.h"

#include "SimpleQuestLog.h"

void UKillClassQuestObjective::TryCompleteObjective_Implementation(UObject* InTargetObject)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UKillClassQuestObjective::TryCompleteObjective_Implementation checked: %s"), *InTargetObject->GetName());
	//if (InTargetObject->IsA(GetTargetClass()))
	//{
	//	UE_LOG(LogSimpleQuest, Verbose, TEXT("UKillClassQuestObjective::TryCompleteObjective_Implementation detected a match"));
		SetCurrentElements(GetCurrentElements() + 1);
		if (GetCurrentElements() >= GetMaxElements())
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("UKillClassQuestObjective::TryCompleteObjective_Implementation completed objective: %s"), *GetFullName());
			CompleteObjective(true);
			return;
		}
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UKillClassQuestObjective::TryCompleteObjective_Implementation did not complete: %s"), *GetFullName());
	//}
	/*else
	{
		UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UKillClassQuestObjective::TryCompleteObjective_Implementation failed to match"));
	}*/
}
