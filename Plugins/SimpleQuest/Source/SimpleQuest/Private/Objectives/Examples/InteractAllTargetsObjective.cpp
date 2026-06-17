// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Objectives/Examples/InteractAllTargetsObjective.h"
#include "NativeGameplayTags.h"
#include "GameFramework/Actor.h"
#include "SimpleQuestLog.h"

UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_AllTargetsInteracted, "SimpleQuest.Outcome.AllTargetsInteracted")


UInteractAllTargetsObjective::UInteractAllTargetsObjective()
{
	AllTargetsInteractedOutcomeTag = Tag_Outcome_AllTargetsInteracted;
}

void UInteractAllTargetsObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveActivationContext& Params)
{
	Super::OnObjectiveActivated_Implementation(Params);

	// Discover the target set via QSS — every Trigger Component currently registered against this Step's tag
	// (canonical + alias forms, via the bidirectional synonym walk in §4.33 Phase 1).
	const TArray<FQuestRoleSourceInfo> Triggers = GetTriggersTargetingThisStep();

	PendingTargetActors.Reset();
	SatisfiedTargetActors.Reset();
	for (const FQuestRoleSourceInfo& Info : Triggers)
	{
		if (AActor* Actor = Info.LiveActor.Get())
		{
			PendingTargetActors.Add(Actor);  // TSet dedupes actor-level (multiple components on one actor collapse).
		}
	}

	if (PendingTargetActors.IsEmpty())
	{
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("UInteractAllTargetsObjective::OnObjectiveActivated : no Trigger Components discovered for step '%s'. "
				 "Objective will stay open indefinitely. Verify trigger actors are placed with the correct StepTagsToTrigger."),
			*GetOwningStepTag().ToString());
	}
	else
	{
		UE_LOG(LogSimpleQuestActivation, Verbose,
			TEXT("UInteractAllTargetsObjective::OnObjectiveActivated : tracking %d target(s) for step '%s'."),
			PendingTargetActors.Num(), *GetOwningStepTag().ToString());
	}
}

void UInteractAllTargetsObjective::TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& InContext)
{
	AActor* Firing = InContext.TriggeredActor;
	if (!Firing) return;

	// Quick membership check — only react to fires from actors in the discovered target set. Trigger fires from
	// actors outside the set (e.g., adopter has unrelated triggers on the same channel) are silently ignored.
	const TWeakObjectPtr<AActor> WeakFiring(Firing);
	if (!PendingTargetActors.Contains(WeakFiring))
	{
		// Either already satisfied (repeat fire from same actor) or never was a target. Either way, no-op.
		return;
	}

	// Tick it off — move from Pending to Satisfied, notify the per-actor satisfaction surface.
	PendingTargetActors.Remove(WeakFiring);
	SatisfiedTargetActors.Add(WeakFiring);

	UE_LOG(LogSimpleQuestActivation, Verbose,
		TEXT("UInteractAllTargetsObjective::TryCompleteObjective : actor '%s' satisfied (%d / %d targets done) — step '%s'."),
		*Firing->GetActorNameOrLabel(),
		SatisfiedTargetActors.Num(),
		SatisfiedTargetActors.Num() + PendingTargetActors.Num(),
		*GetOwningStepTag().ToString());

	PublishTriggerSatisfied(InContext);
	ReportProgress(InContext);

	// Check completion. Prune dead weak refs first so a target actor destroyed mid-step doesn't permanently
	// gate completion.
	if (PruneAndCountPending() == 0)
	{
		UE_LOG(LogSimpleQuestActivation, Verbose,
			TEXT("UInteractAllTargetsObjective::TryCompleteObjective : all targets satisfied for step '%s' — completing."),
			*GetOwningStepTag().ToString());
		CompleteObjectiveWithOutcome(AllTargetsInteractedOutcomeTag, NAME_None, InContext);
	}
}

int32 UInteractAllTargetsObjective::PruneAndCountPending()
{
	for (auto It = PendingTargetActors.CreateIterator(); It; ++It)
	{
		if (!It->IsValid()) It.RemoveCurrent();
	}
	return PendingTargetActors.Num();
}

