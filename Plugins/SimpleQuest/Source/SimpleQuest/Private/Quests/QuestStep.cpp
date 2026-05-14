// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestStep.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "SimpleQuestLog.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
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
	// Compose activation params FIRST, before Super::ActivateInternal fires OnNodeStarted. The base-class Activate-
	// Internal's OnNodeStarted broadcast routes to UQuestManagerSubsystem::HandleOnNodeStarted, whose Step-side
	// RecordEntry call reads GetReceivedActivationParams() for the registry snapshot. Populating ReceivedActivation-
	// Params before Super means the snapshot captures the merged final params (Instigator, Provenance, the full
	// FQuestObjectiveActivationContext shape) rather than the default-constructed empty struct.
	//
	// Composition rules: additive for compound fields (sets union, counts sum); Instigator + CustomData come
	// straight from external since Step has no equivalents. Position data (if any) goes through CustomData. Provenance
	// + IncomingOutcomeTag propagate so the registry's per-start record knows how this start was initiated and which
	// outcome (if any) drove it.
	FQuestObjectiveActivationContext Context;

	// Designer-authored from this Step's UPROPERTYs (TargetActors here is the Step's authored set;
	// PendingActivationParams.Dynamic.TargetActors is the runtime-supplied appendage):
	Context.Authored.TargetClasses = TargetClasses;
	Context.Authored.NumElementsRequired = NumberOfElements + PendingActivationContext.Authored.NumElementsRequired;

	// Runtime-dynamic merge — append incoming runtime contributions onto this Step's runtime set:
	Context.Dynamic.TargetActors = TargetActors;
	Context.Dynamic.TargetActors.Append(PendingActivationContext.Dynamic.TargetActors);
	Context.Dynamic.Instigator = PendingActivationContext.Dynamic.Instigator;
	Context.Dynamic.CustomData = PendingActivationContext.Dynamic.CustomData;
	Context.Dynamic.OriginTag = PendingActivationContext.Dynamic.OriginTag;
	Context.Dynamic.OriginChain = PendingActivationContext.Dynamic.OriginChain;
	Context.Dynamic.Provenance = PendingActivationContext.Dynamic.Provenance;
	Context.Dynamic.IncomingOutcomeTag = PendingActivationContext.Dynamic.IncomingOutcomeTag;

	// Snapshot the composed params before Super so OnNodeStarted's handler reads a populated struct. Also serves
	// Piece D chain propagation — ChainToNextNodes reads OriginChain to extend the forwarded chain when the chain
	// reaches a downstream step.
	ReceivedActivationContext = Context;

	// Now fire OnNodeStarted (via Super::ActivateInternal). HandleOnNodeStarted runs SetQuestLive, publishes
	// FQuestStartedEvent, and captures the Step-side entry record using the snapshot above.
	Super::ActivateInternal(InContextualTag);

	UClass* ObjClass = QuestObjective.LoadSynchronous();
	if (!ObjClass) return;

	LiveObjective = NewObject<UQuestObjective>(this, ObjClass);
	LiveObjective->OnQuestObjectiveComplete.AddDynamic(this, &UQuestStep::OnObjectiveComplete);
	LiveObjective->OnQuestObjectiveProgress.AddDynamic(this, &UQuestStep::OnObjectiveProgress);

	// Consume and clear so subsequent activations don't accidentally reuse stale external params.
	PendingActivationContext = FQuestObjectiveActivationContext{};

	LiveObjective->DispatchOnObjectiveActivated(Context);
}

void UQuestStep::DeactivateInternal(FGameplayTag InContextualTag)
{
	if (LiveObjective)
	{
		// Symmetric to OnObjectiveActivated: fire the deactivation hook BEFORE delegate cleanup and null-out
		// so subclass overrides (universal-adapter pattern: subscribed to game-system events in OnObjective-
		// Activated) can still inspect targets / objective state and explicitly unsubscribe.
		LiveObjective->DispatchOnObjectiveDeactivated();
		LiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		LiveObjective->OnQuestObjectiveProgress.RemoveDynamic(this, &UQuestStep::OnObjectiveProgress);
		LiveObjective = nullptr;
	}
	ReceivedActivationContext = FQuestObjectiveActivationContext{};
	CompletionForwardParams = FQuestObjectiveActivationContext{};
	Super::DeactivateInternal(InContextualTag);
}

void UQuestStep::ResetTransientState()
{
	Super::ResetTransientState();
	// LiveObjective was a weak tie to the prior PIE's world — don't touch it (GC cleaned up the UObject), just
	// drop the reference. CompletionContext + Piece D params are pure value types; reset to empty.
	LiveObjective = nullptr;
	CompletionContext = FQuestObjectiveTriggerContext{};
	ReceivedActivationContext = FQuestObjectiveActivationContext{};
	CompletionForwardParams = FQuestObjectiveActivationContext{};
}

void UQuestStep::OnObjectiveComplete(FGameplayTag OutcomeTag, FName PathIdentity)
{
	if (LiveObjective)
	{
		// Fire the deactivation hook FIRST, before TakeCompletionContext / TakeForwardActivationParams move
		// data out of the objective, so the subclass override can read CompletionContext / ForwardActivation-
		// Params if it needs them. The objective is still live (we're inside its OnQuestObjectiveComplete
		// broadcast); ConditionalBeginDestroy hasn't fired yet.
		LiveObjective->DispatchOnObjectiveDeactivated();
		CompletionContext = LiveObjective->TakeCompletionContext();
		CompletionForwardParams = LiveObjective->TakeForwardActivationParams();
		LiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		LiveObjective->OnQuestObjectiveProgress.RemoveDynamic(this, &UQuestStep::OnObjectiveProgress);
		LiveObjective = nullptr;
	}
	OnNodeCompleted.ExecuteIfBound(this, OutcomeTag, PathIdentity);
}

void UQuestStep::OnObjectiveProgress(FQuestObjectiveTriggerContext ProgressContext)
{
	OnNodeProgress.ExecuteIfBound(this, ProgressContext);
}
