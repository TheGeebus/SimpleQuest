// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Objectives/CountingQuestObjective.h"
#include "SimpleQuestLog.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"


void UCountingQuestObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveAuthoredConfig& Authored, const FQuestObjectiveRuntimeContext& Runtime)
{
	Super::OnObjectiveActivated_Implementation(Authored, Runtime);
	MaxElements = Authored.NumElementsRequired + Runtime.IncomingContext.Config.NumElementsRequired;
	CurrentElements = 0;
}

bool UCountingQuestObjective::AddProgress(const FQuestObjectiveTriggerContext& InContext, FGameplayTag OutcomeTag, int32 Amount)
{
	CurrentElements = FMath::Clamp(CurrentElements + Amount, 0, MaxElements);

	FQuestObjectiveTriggerContext OutContext = InContext;
	OutContext.CurrentCount = CurrentElements;
	OutContext.RequiredCount = MaxElements;
	ReportProgress(OutContext);
	
	if (CurrentElements >= MaxElements)
	{
		CompleteObjectiveWithOutcome(OutcomeTag, NAME_None, OutContext);
		return true;
	}
	return false;
}

void UCountingQuestObjective::SetCurrentElements(const int32 NewAmount)
{
	if (CurrentElements != NewAmount && NewAmount <= MaxElements)
	{
		CurrentElements = NewAmount;
		FQuestObjectiveTriggerContext ProgressContext;
		ProgressContext.CurrentCount = CurrentElements;
		ProgressContext.RequiredCount = MaxElements;
		ReportProgress(ProgressContext);
	}
}