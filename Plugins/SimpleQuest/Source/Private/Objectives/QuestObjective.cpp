// Fill out your copyright notice in the Description page of Project Settings.


#include "Objectives/QuestObjective.h"

#include "SimpleQuestLog.h"
#include "Interfaces/QuestTargetInterface.h"


void UQuestObjective::TryCompleteObjective_Implementation(UObject* InTargetObject)
{
	UE_LOG(LogSimpleQuest, Warning, TEXT("Called parent UQuestObjective::TryCompleteObjective. Override this event to provide quest completion logic."));
}

void UQuestObjective::SetObjectiveTarget_Implementation(int32 InStepID, const TSet<TSoftObjectPtr<AActor>>& InTargetActors, UClass* InTargetClass,
	int32 NumElementsRequired, bool bUseCounter)
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("Called parent UQuestObjective::SetObjectiveTarget_Implementation. Set default values."))
	StepID = InStepID;
	TargetActors = InTargetActors;
	TargetClass = InTargetClass;
	MaxElements = NumElementsRequired;
	bUseQuestCounter = bUseCounter;
	SetCurrentElements(0);
	/*
	if (!TargetActors.IsEmpty())
	{
		for (const auto Actor : TargetActors )
		{
			if (IQuestTargetInterface* Interface = Actor->FindComponentByInterface<IQuestTargetInterface>())
			{
				UObject* ObjectPtr = Cast<UObject>(Interface);
				FScriptInterface ScriptInterface;
				ScriptInterface.SetInterface(Interface);
				ScriptInterface.SetObject(ObjectPtr);
				QuestTargetInterfaces.Add(ScriptInterface);
			}			
		}
	}
	*/
}

void UQuestObjective::CompleteObjective(bool bDidSucceed)
{
	bStepCompleted = true;
	OnQuestObjectiveComplete.Broadcast(StepID, bDidSucceed);
	ConditionalBeginDestroy();
}

void UQuestObjective::EnableTargetObject(UObject* Target, bool bIsTargetEnabled) const
{
	OnEnableTarget.Broadcast(Target, GetStepID(), bIsTargetEnabled);
}

void UQuestObjective::EnableQuestTargetActorSet(bool bIsTargetEnabled)
{
	for (const auto Target : TargetActors)
	{
		if (AActor* TargetActor = Target.LoadSynchronous())
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestObjective::EnableQuestTargetActor : enabling target actor: %s"), *TargetActor->GetFName().ToString());
			EnableTargetObject(TargetActor, bIsTargetEnabled);
		}
	}
}

void UQuestObjective::EnableQuestTargetClass(bool bIsTargetEnabled) const
{
	OnEnableTarget.Broadcast(GetTargetClass(), GetStepID(), bIsTargetEnabled);
}

void UQuestObjective::SetCurrentElements(const int32 NewAmount)
{
	if (CurrentElements != NewAmount && NewAmount <= MaxElements)
	{
		CurrentElements = NewAmount;
	}
}

