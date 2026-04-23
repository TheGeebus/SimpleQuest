// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Objectives/CountingQuestObjective.h"
#include "SimpleQuestLog.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"


void UCountingQuestObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveActivationParams& Params)
{
	Super::OnObjectiveActivated_Implementation(Params);
	MaxElements = Params.NumElementsRequired;
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