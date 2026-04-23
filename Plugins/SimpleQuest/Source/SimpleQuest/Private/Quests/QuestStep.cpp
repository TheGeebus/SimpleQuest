// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestStep.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "SimpleQuestLog.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "WorldState/WorldStateSubsystem.h"

void UQuestStep::Activate(FGameplayTag InContextualTag)
{
	if (IsGiverGated())
	{
		// Giver semantics: prerequisites gate activation, same as base class.
		Super::Activate(InContextualTag);
		return;
	}

	// No giver: activate immediately and let prerequisites gate progression or completion according to PrerequisiteGateMode.
	ActivateInternal(InContextualTag);
}

void UQuestStep::ActivateInternal(FGameplayTag InContextualTag)
{
	Super::ActivateInternal(InContextualTag);

	UClass* ObjClass = QuestObjective.LoadSynchronous();
	if (!ObjClass) return;

	ActiveObjective = NewObject<UQuestObjective>(this, ObjClass);
	ActiveObjective->OnQuestObjectiveComplete.AddDynamic(this, &UQuestStep::OnObjectiveComplete);
	ActiveObjective->OnQuestObjectiveProgress.AddDynamic(this, &UQuestStep::OnObjectiveProgress);

	// Compose activation params — Step's authored defaults plus any external params from a
	// Tag_Channel_QuestActivationRequest publisher. Additive for compound fields (sets union, count sums);
	// ActivationSource + CustomData come straight from external since Step has no equivalents. Position
	// data (if any) goes through CustomData — no first-class TargetVector field on the Step anymore.
	FQuestObjectiveActivationParams Params;

	Params.TargetActors = TargetActors;
	Params.TargetActors.Append(PendingActivationParams.TargetActors);

	Params.TargetClasses = TargetClasses;
	Params.TargetClasses.Append(PendingActivationParams.TargetClasses);

	Params.NumElementsRequired = NumberOfElements + PendingActivationParams.NumElementsRequired;

	Params.ActivationSource = PendingActivationParams.ActivationSource;
	Params.CustomData = PendingActivationParams.CustomData;

	Params.OriginTag = PendingActivationParams.OriginTag;
	Params.OriginChain = PendingActivationParams.OriginChain;

	// Snapshot the composed params for Piece D chain propagation — ChainToNextNodes needs OriginChain to extend the
	// forwarded chain with this step's tag when the chain reaches a downstream step.
	ReceivedActivationParams = Params;

	// Consume + clear so subsequent activations don't accidentally reuse stale external params.
	PendingActivationParams = FQuestObjectiveActivationParams{};

	ActiveObjective->DispatchOnObjectiveActivated(Params);
}

void UQuestStep::DeactivateInternal(FGameplayTag InContextualTag)
{
	if (ActiveObjective)
	{
		ActiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		ActiveObjective->OnQuestObjectiveProgress.RemoveDynamic(this, &UQuestStep::OnObjectiveProgress);
		ActiveObjective = nullptr;
	}
	ReceivedActivationParams = FQuestObjectiveActivationParams{};
	CompletionForwardParams = FQuestObjectiveActivationParams{};
	Super::DeactivateInternal(InContextualTag);
}

void UQuestStep::ResetTransientState()
{
	Super::ResetTransientState();
	// ActiveObjective was a weak tie to the prior PIE's world — don't touch it (GC cleaned up the UObject), just
	// drop the reference. CompletionData + Piece D params are pure value types; reset to empty.
	ActiveObjective = nullptr;
	CompletionData = FQuestObjectiveContext{};
	ReceivedActivationParams = FQuestObjectiveActivationParams{};
	CompletionForwardParams = FQuestObjectiveActivationParams{};
}

void UQuestStep::OnObjectiveComplete(FGameplayTag OutcomeTag)
{
	if (ActiveObjective)
	{
		CompletionData = ActiveObjective->TakeCompletionData();
		CompletionForwardParams = ActiveObjective->TakeForwardActivationParams();
		ActiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		ActiveObjective->OnQuestObjectiveProgress.RemoveDynamic(this, &UQuestStep::OnObjectiveProgress);
		ActiveObjective = nullptr;
	}
	OnNodeCompleted.ExecuteIfBound(this, OutcomeTag);
}

void UQuestStep::OnObjectiveProgress(FQuestObjectiveContext ProgressData)
{
	OnNodeProgress.ExecuteIfBound(this, ProgressData);
}
