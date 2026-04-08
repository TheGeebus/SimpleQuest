// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Objectives/QuestObjective.h"

#include "GameplayTagContainer.h"
#include "SimpleQuestLog.h"
#include "Interfaces/QuestTargetInterface.h"


void UQuestObjective::TryCompleteObjective_Implementation(UObject* InTargetObject)
{
	UE_LOG(LogSimpleQuest, Warning, TEXT("Called parent UQuestObjective::TryCompleteObjective. Override this event to provide quest completion logic."));
}

void UQuestObjective::SetObjectiveTarget_Implementation(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, UClass* InTargetClass, int32 NumElementsRequired, bool bUseCounter)
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("Called parent UQuestObjective::SetObjectiveTarget_Implementation. Set default values."))
	TargetActors = InTargetActors;
	TargetClass = InTargetClass;
	MaxElements = NumElementsRequired;
	bUseQuestCounter = bUseCounter;
	SetCurrentElements(0);
}

bool UQuestObjective::IsObjectRelevant_Implementation(UObject* InTargetObject)
{
	bool bIsTargetRelevant = false;
	const bool bHasTargetClass = IsValid(TargetClass);
	if (bHasTargetClass)
	{
		if (InTargetObject->IsA(TargetClass))
		{
			bIsTargetRelevant = true; 
		}
	}
	
	if (!TargetActors.IsEmpty())
	{
		if (const AActor* AsActor = Cast<AActor>(InTargetObject))
		{
			for (const TSoftObjectPtr<AActor>& SoftTarget : TargetActors)
			{
				if (SoftTarget.Get() == AsActor)
				{
					bIsTargetRelevant = true;
					break;
				}
			}
		}
	}
	else if (!bHasTargetClass)
	{
		bIsTargetRelevant = true; // Neither TargetActors nor TargetClass filters are set, anything is relevant
	}
	
	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestObjective::IsObjectRelevant_Implementation : %s is relevant to %s: %hs"), *InTargetObject->GetName(), *this->GetName(), bIsTargetRelevant ? "true" : "false");
	return bIsTargetRelevant;
}

void UQuestObjective::CompleteObjectiveWithOutcome(FGameplayTag OutcomeTag)
{
	bStepCompleted = true;
	OnQuestObjectiveComplete.Broadcast(OutcomeTag);
	ConditionalBeginDestroy();
}

void UQuestObjective::EnableTargetObject(UObject* Target, bool bIsTargetEnabled) const
{
	OnEnableTarget.Broadcast(Target, bIsTargetEnabled);
}

void UQuestObjective::EnableQuestTargetActors(bool bIsTargetEnabled)
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
	OnEnableTarget.Broadcast(GetTargetClass(), bIsTargetEnabled);
}

void UQuestObjective::SetCurrentElements(const int32 NewAmount)
{
	if (CurrentElements != NewAmount && NewAmount <= MaxElements)
	{
		CurrentElements = NewAmount;
	}
}

