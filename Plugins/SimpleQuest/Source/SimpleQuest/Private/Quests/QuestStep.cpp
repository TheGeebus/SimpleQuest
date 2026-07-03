// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestStep.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "Quests/Types/QuestObjectiveAuthoredConfig.h"
#include "Subsystems/QuestStateSubsystem.h"

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
	// Pack the authored config from this Step's UPROPERTYs and take the caller's input verbatim as the runtime context —
	// no merge. The objective composes the two however it wants (see UQuestObjective::OnObjectiveActivated), with full
	// provenance over which values are authored vs caller-supplied. ReceivedActivationContext (the runtime half) is set
	// BEFORE Super::ActivateInternal so OnNodeStarted's handler reads a populated snapshot for the registry's start record.
	const FQuestObjectiveAuthoredConfig Authored = BuildAuthoredConfig();
	const FQuestObjectiveRuntimeContext Runtime = PendingActivationContext;   // caller input + framework-stamped provenance/outcome

	ReceivedActivationContext = Runtime;

	// Now fire OnNodeStarted (via Super::ActivateInternal). HandleOnNodeStarted runs SetQuestLive, publishes
	// FQuestStartedEvent, and captures the Step-side entry record using the snapshot above. AssembleEventContext reads
	// PendingActivationContext during that call, so clear it only after Super returns.
	Super::ActivateInternal(InContextualTag);

	PendingActivationContext = FQuestObjectiveRuntimeContext{};   // consume + clear (already copied into Runtime above)

	InstantiateLiveObjective(Authored, Runtime, InContextualTag);
}

FQuestObjectiveAuthoredConfig UQuestStep::BuildAuthoredConfig() const
{
	FQuestObjectiveAuthoredConfig Authored;
	Authored.TargetClasses = TargetClasses;
	Authored.TargetActors = TargetActors;
	Authored.NumElementsRequired = NumberOfElements;
	Authored.ConfigAsset = ConfigAsset;
	return Authored;
}

void UQuestStep::InstantiateLiveObjective(const FQuestObjectiveAuthoredConfig& Authored, const FQuestObjectiveRuntimeContext& Runtime, FGameplayTag InContextualTag)
{
	UClass* ObjClass = QuestObjective.LoadSynchronous();
	if (!ObjClass) return;

	LiveObjective = NewObject<UQuestObjective>(this, ObjClass);
	LiveObjective->OnQuestObjectiveComplete.AddDynamic(this, &UQuestStep::OnObjectiveComplete);
	LiveObjective->OnQuestObjectiveProgress.AddDynamic(this, &UQuestStep::OnObjectiveProgress);
	LiveObjective->OnQuestObjectiveRefused.AddDynamic(this, &UQuestStep::OnObjectiveRefused);
	LiveObjective->OnQuestObjectiveTriggerDeactivation.AddDynamic(this, &UQuestStep::OnObjectiveTriggerDeactivation);
	LiveObjective->OnQuestObjectiveTriggerSatisfied.AddDynamic(this, &UQuestStep::OnObjectiveTriggerSatisfied);

	if (const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UQuestStateSubsystem* StateSubsystem = GI->GetSubsystem<UQuestStateSubsystem>())
		{
			TArray<FGameplayTag> ObjectiveKeys;
			ObjectiveKeys.Add(InContextualTag);
			for (const FGameplayTag& Alias : AssetScopedAliasTags)
			{
				if (Alias.IsValid()) ObjectiveKeys.AddUnique(Alias);
			}
			StateSubsystem->RegisterActiveObjective(LiveObjective, ObjectiveKeys);
		}
	}

	LiveObjective->DispatchOnObjectiveActivated(Authored, Runtime, InContextualTag);
}

void UQuestStep::RestoreObjective(const FQuestObjectiveActivationContext& IncomingContext, FGameplayTag InContextualTag)
{
	// Rebuild the runtime context from the saved snapshot: the caller's incoming half, plus a Restored provenance stamp
	// so the objective can suppress first-activation side effects. The authored half re-derives from this Step.
	FQuestObjectiveRuntimeContext Runtime;
	Runtime.IncomingContext = IncomingContext;
	Runtime.Provenance = EQuestActivationProvenance::Restored;

	// Mirror onto the Step so post-restore reads (completion chaining, AssembleEventContext) see the context a live
	// activation would have left behind. No Super::ActivateInternal here — SetQuestLive / lifecycle events / the entry
	// record all fired at the original start and were restored in bulk; re-firing them would double-count.
	ReceivedActivationContext = Runtime;

	InstantiateLiveObjective(BuildAuthoredConfig(), Runtime, InContextualTag);
}

void UQuestStep::DeactivateInternal(FGameplayTag InContextualTag)
{
	if (LiveObjective)
	{
		// Symmetric to OnObjectiveActivated: fire the deactivation hook BEFORE delegate cleanup and null-out
		// so subclass overrides (universal-adapter pattern: subscribed to game-system events in OnObjective-
		// Activated) can still inspect targets / objective state and explicitly unsubscribe.
		LiveObjective->DispatchOnObjectiveDeactivated();
		UnregisterObjectiveFromQuestStateSubsystem(LiveObjective, GetWorld());
		LiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		LiveObjective->OnQuestObjectiveProgress.RemoveDynamic(this, &UQuestStep::OnObjectiveProgress);
		LiveObjective->OnQuestObjectiveRefused.RemoveDynamic(this, &UQuestStep::OnObjectiveRefused);
		LiveObjective->OnQuestObjectiveTriggerDeactivation.RemoveDynamic(this, &UQuestStep::OnObjectiveTriggerDeactivation);
		LiveObjective->OnQuestObjectiveTriggerSatisfied.RemoveDynamic(this, &UQuestStep::OnObjectiveTriggerSatisfied);
		LiveObjective = nullptr;
	}
	ReceivedActivationContext = FQuestObjectiveRuntimeContext{};
	CompletionForwardParams = FQuestObjectiveActivationContext{};
	Super::DeactivateInternal(InContextualTag);
}

void UQuestStep::ResetTransientState()
{
	Super::ResetTransientState();
	
	// LiveObjective was a weak tie to the prior PIE's world — don't touch it (GC cleaned up the UObject), just
	// drop the reference. CompletionContext + params are pure value types; reset to empty. QSS unregister:
	// ActiveObjectivesByTag uses TWeakObjectPtr — stale entries become invalid on dereference and queries skip them.
	// Explicit cleanup belongs in the manager's PIE-reset path if/when needed.
	LiveObjective = nullptr;
	CompletionContext = FQuestObjectiveTriggerContext{};
	ReceivedActivationContext = FQuestObjectiveRuntimeContext{};
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
		UnregisterObjectiveFromQuestStateSubsystem(LiveObjective, GetWorld());
		CompletionContext = LiveObjective->TakeCompletionContext();
		CompletionForwardParams = LiveObjective->TakeForwardActivationParams();
		LiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
		LiveObjective->OnQuestObjectiveProgress.RemoveDynamic(this, &UQuestStep::OnObjectiveProgress);
		LiveObjective->OnQuestObjectiveRefused.RemoveDynamic(this, &UQuestStep::OnObjectiveRefused);
		LiveObjective->OnQuestObjectiveTriggerDeactivation.RemoveDynamic(this, &UQuestStep::OnObjectiveTriggerDeactivation);
		LiveObjective->OnQuestObjectiveTriggerSatisfied.RemoveDynamic(this, &UQuestStep::OnObjectiveTriggerSatisfied);
		LiveObjective = nullptr;
	}
	OnNodeCompleted.ExecuteIfBound(this, OutcomeTag, PathIdentity);
}

void UQuestStep::OnObjectiveProgress(FQuestObjectiveTriggerContext ProgressContext)
{
	OnNodeProgress.ExecuteIfBound(this, ProgressContext);
}

void UQuestStep::OnObjectiveRefused(FGameplayTag RefusalReason, FQuestObjectiveTriggerContext TriggerContext)
{
	OnNodeRefused.ExecuteIfBound(this, RefusalReason, TriggerContext);
}

void UQuestStep::OnObjectiveTriggerDeactivation(FGameplayTag OutcomeTag, FQuestObjectiveTriggerContext FinalContext)
{
	OnNodeTriggerDeactivation.ExecuteIfBound(this, OutcomeTag, FinalContext);
}

void UQuestStep::OnObjectiveTriggerSatisfied(FQuestObjectiveTriggerContext Context)
{
	OnNodeTriggerSatisfied.ExecuteIfBound(this, Context);
}

void UQuestStep::UnregisterObjectiveFromQuestStateSubsystem(UQuestObjective* Objective, const UWorld* World)
{
	if (!Objective || !World) return;
	if (const UGameInstance* GI = World->GetGameInstance())
	{
		if (UQuestStateSubsystem* StateSubsystem = GI->GetSubsystem<UQuestStateSubsystem>())
		{
			StateSubsystem->UnregisterActiveObjective(Objective);
		}
	}
}
