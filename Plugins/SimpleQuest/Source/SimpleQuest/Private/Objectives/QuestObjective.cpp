// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Objectives/QuestObjective.h"

#include "GameplayTagContainer.h"
#include "SimpleQuestLog.h"
#include "Interfaces/QuestTargetInterface.h"


void UQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveContext& InContext)
{
	/*-------------------------------------------------------------------------------------------------------------------*
	 * Set fields on an FQuestObjectiveContext and pass it to CompleteObjectiveWithOutcome.
	 * Common fields:
	 *   InContext can be forwarded directly for pass-through, or build a new one:
	 *   FQuestObjectiveContext OutContext;
	 *   OutContext.TriggeredActor = InContext.TriggeredActor;
	 *   OutContext.Instigator = InContext.Instigator;
	 * Game-specific extension - any desired struct type, such as example user-defined struct FMyKillData:
	 *   OutContext.CustomData = FInstancedStruct::Make<FMyKillData>(Target->GetFName(), DamageType, ...);
	 *-------------------------------------------------------------------------------------------------------------------*/

	UE_LOG(LogSimpleQuest, Warning, TEXT("Called parent UQuestObjective::TryCompleteObjective. Override this event to provide quest completion logic."));
}

void UQuestObjective::SetObjectiveTarget_Implementation(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, const TSet<TSubclassOf<AActor>>& InTargetClasses, int32 NumElementsRequired)
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("Called parent UQuestObjective::SetObjectiveTarget_Implementation. Set default values."))
	TargetActors = InTargetActors;
	TargetClasses = InTargetClasses;
}

void UQuestObjective::DispatchSetObjectiveTarget(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, const TSet<TSubclassOf<AActor>>& InTargetClasses, int32 NumElementsRequired)
{
	SetObjectiveTarget(InTargetActors, InTargetClasses, NumElementsRequired);
}

void UQuestObjective::DispatchTryCompleteObjective(const FQuestObjectiveContext& InContext)
{
	TryCompleteObjective(InContext);
}

TArray<FGameplayTag> UQuestObjective::GetPossibleOutcomes() const
{
	return {};
}

void UQuestObjective::CompleteObjectiveWithOutcome(FGameplayTag OutcomeTag, const FQuestObjectiveContext& InCompletionData)
{
	CompletionData = InCompletionData;
	OnQuestObjectiveComplete.Broadcast(OutcomeTag);
	ConditionalBeginDestroy();
}

void UQuestObjective::ReportProgress(const FQuestObjectiveContext& InProgressData)
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("ReportProgress: %d/%d — %s"), InProgressData.CurrentCount, InProgressData.RequiredCount, *GetFullName());
	OnQuestObjectiveProgress.Broadcast(InProgressData);
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

void UQuestObjective::EnableQuestTargetClasses(bool bIsTargetEnabled) const
{
	for (const TSubclassOf<AActor>& Class : TargetClasses)
	{
		if (Class) OnEnableTarget.Broadcast(Class.Get(), bIsTargetEnabled);
	}
}

