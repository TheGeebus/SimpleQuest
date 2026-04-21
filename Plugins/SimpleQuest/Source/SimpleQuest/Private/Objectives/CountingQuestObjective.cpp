// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Objectives/CountingQuestObjective.h"

#include "SimpleQuestLog.h"

void UCountingQuestObjective::SetObjectiveTarget_Implementation(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, const TSet<TSubclassOf<AActor>>& InTargetClasses, int32 NumElementsRequired)
{
	Super::SetObjectiveTarget_Implementation(InTargetActors, InTargetClasses, NumElementsRequired);
	MaxElements = NumElementsRequired;
	CurrentElements = 0;
}

bool UCountingQuestObjective::AddProgress(const FQuestObjectiveContext& InContext, FGameplayTag OutcomeTag, int32 Amount)
{
	CurrentElements = FMath::Clamp(CurrentElements + Amount, 0, MaxElements);

	FQuestObjectiveContext OutContext = InContext;
	OutContext.CurrentCount = CurrentElements;
	OutContext.RequiredCount = MaxElements;

	if (CurrentElements >= MaxElements)
	{
		CompleteObjectiveWithOutcome(OutcomeTag, OutContext);
		return true;
	}

	ReportProgress(OutContext);
	return false;
}

void UCountingQuestObjective::SetCurrentElements(const int32 NewAmount)
{
	if (CurrentElements != NewAmount && NewAmount <= MaxElements)
	{
		CurrentElements = NewAmount;
		FQuestObjectiveContext ProgressContext;
		ProgressContext.CurrentCount = CurrentElements;
		ProgressContext.RequiredCount = MaxElements;
		ReportProgress(ProgressContext);
	}
}