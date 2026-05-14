// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#include "Objectives/QuestObjective.h"

#include "GameplayTagContainer.h"
#include "SimpleQuestLog.h"
#include "Interfaces/QuestTargetInterface.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"


void UQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& InContext)
{
	/*-------------------------------------------------------------------------------------------------------------------*
	 * Set fields on an FQuestObjectiveTriggerContext and pass it to CompleteObjectiveWithOutcome.
	 * Common fields:
	 *   InContext can be forwarded directly for pass-through, or build a new one:
	 *   FQuestObjectiveTriggerContext OutContext;
	 *   OutContext.TriggeredActor = InContext.TriggeredActor;
	 *   OutContext.Instigator = InContext.Instigator;
	 * Game-specific extension - any desired struct type, such as example user-defined struct FMyKillData:
	 *   OutContext.CustomData = FInstancedStruct::Make<FMyKillData>(Target->GetFName(), DamageType, ...);
	 *-------------------------------------------------------------------------------------------------------------------*/

	UE_LOG(LogSimpleQuest, Warning, TEXT("Called parent UQuestObjective::TryCompleteObjective. Override this event to provide quest completion logic."));
}

void UQuestObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveActivationContext& Params)
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestObjective::OnObjectiveActivated_Implementation — storing base target fields from activation params."));
	TargetActors = Params.Dynamic.TargetActors;
	TargetClasses = Params.Authored.TargetClasses;
}

void UQuestObjective::DispatchOnObjectiveActivated(const FQuestObjectiveActivationContext& Params)
{
	OnObjectiveActivated(Params);
}

void UQuestObjective::DispatchTryCompleteObjective(const FQuestObjectiveTriggerContext& InContext)
{
	TryCompleteObjective(InContext);
}

void UQuestObjective::DispatchOnObjectiveDeactivated()
{
	OnObjectiveDeactivated();
}

void UQuestObjective::OnObjectiveDeactivated_Implementation()
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestObjective::OnObjectiveDeactivated_Implementation — base no-op. Override in "
		"subclass to unsubscribe from external event sources, tear down UI handles, release timers, etc. (%s)"), *GetFullName());
}

TArray<FGameplayTag> UQuestObjective::GetPossibleOutcomes() const
{
	return {};
}

void UQuestObjective::CompleteObjectiveWithOutcome(FGameplayTag OutcomeTag, FName PathIdentity, const FQuestObjectiveTriggerContext& InCompletionContext, const FQuestObjectiveActivationContext& InForwardParams)
{
	CompletionContext = InCompletionContext;
	ForwardActivationParams = InForwardParams;
	// Auto-derive PathIdentity from OutcomeTag.GetTagName() when caller didn't supply one explicitly. Static K2
	// placements supply NAME_None and depend on this fallback for back-compat; dynamic K2 placements (Bundle Y)
	// supply an explicit PathIdentity from the node's authored PathName.
	const FName ResolvedPath = PathIdentity.IsNone() ? OutcomeTag.GetTagName() : PathIdentity;
	OnQuestObjectiveComplete.Broadcast(OutcomeTag, ResolvedPath);
	ConditionalBeginDestroy();
}

void UQuestObjective::ReportProgress(const FQuestObjectiveTriggerContext& ProgressContext)
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("ReportProgress: %d/%d — %s"), ProgressContext.CurrentCount, ProgressContext.RequiredCount, *GetFullName());
	OnQuestObjectiveProgress.Broadcast(ProgressContext);
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
	for (const TSoftClassPtr<AActor>& SoftClass  : TargetClasses)
	{
		// Synchronous load at use time — designer authored a soft ref, hard UClass is only needed here.
		if (UClass* Loaded = SoftClass.LoadSynchronous())
		{
			OnEnableTarget.Broadcast(Loaded, bIsTargetEnabled);
		}
	}
}

