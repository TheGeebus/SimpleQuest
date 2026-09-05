// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestStep.h"

#include "SimpleQuestLog.h"
#include "TimerManager.h"
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
	// Pack the authored config from this Step's UPROPERTYs and take the caller's input verbatim as the runtime context -
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

	// A prior objective can still be here: a completion releases at the end of its frame, and a chain that loops back
	// to this Step lands inside that frame. Release it properly rather than overwriting the pointer and leaving it
	// holding our bindings with nothing left to release it.
	ReleaseLiveObjective();

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
	// activation would have left behind. No Super::ActivateInternal here - SetQuestLive / lifecycle events / the entry
	// record all fired at the original start and were restored in bulk; re-firing them would double-count.
	ReceivedActivationContext = Runtime;

	InstantiateLiveObjective(BuildAuthoredConfig(), Runtime, InContextualTag);
}

void UQuestStep::DeactivateInternal(FGameplayTag InContextualTag)
{
	// *** ONLY THE INTERRUPTION PATH RELEASES HERE. *** A completed objective was already dispatched and unregistered
	// at completion and is already queued for release at the end of that frame. Deactivation can arrive inside the
	// same frame - a completion whose cascade tears this step down - and releasing here would undo the very boundary
	// that makes post-completion work legal. Re-firing the hook would also tell a subclass it was Interrupted
	// immediately after telling it it Completed.
	if (LiveObjective && !LiveObjective->HasCompleted())
	{
		// Symmetric to OnObjectiveActivated: fire the deactivation hook BEFORE the unbind so subclass overrides
		// (universal-adapter pattern: subscribed to game-system events in OnObjectiveActivated) can still inspect
		// targets / objective state and explicitly unsubscribe.
		LiveObjective->DispatchOnObjectiveDeactivated(EQuestObjectiveDeactivationReason::Interrupted);
		UnregisterObjectiveFromQuestStateSubsystem(LiveObjective, GetWorld());
		ReleaseLiveObjective();
	}
	ReceivedActivationContext = FQuestObjectiveRuntimeContext{};
	CompletionForwardParams = FQuestObjectiveActivationContext{};
	Super::DeactivateInternal(InContextualTag);
}

void UQuestStep::ResetTransientState()
{
	Super::ResetTransientState();
	
	// LiveObjective was a weak tie to the prior PIE's world - don't touch it (GC cleaned up the UObject), just
	// drop the reference. CompletionContext + params are pure value types; reset to empty. QSS unregister:
	// ActiveObjectivesByTag uses TWeakObjectPtr - stale entries become invalid on dereference and queries skip them.
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
		// broadcast).
		LiveObjective->DispatchOnObjectiveDeactivated(EQuestObjectiveDeactivationReason::Completed);
		UnregisterObjectiveFromQuestStateSubsystem(LiveObjective, GetWorld());
		CompletionContext = LiveObjective->TakeCompletionContext();
		CompletionForwardParams = LiveObjective->TakeForwardActivationParams();

		// *** BINDINGS STAY UP FOR THE REST OF THIS FRAME. *** We are inside the objective's own completion
		// broadcast and the Blueprint chain that called Complete is still running - unbinding here would silently
		// orphan whatever it does next (a PublishTriggerSatisfied, a cleanup publish). So the Step keeps listening
		// and schedules the release for the end of the frame instead: post-completion work is a supported pattern
		// with a definite end, rather than an objective that lives on until the step happens to be deactivated -
		// which, for a step that simply completes, never happens. A second completion cannot slip through; the
		// objective refuses it at the source.
		ScheduleCompletedObjectiveRelease();
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

void UQuestStep::ReleaseLiveObjective()
{
	if (!LiveObjective) return;

	LiveObjective->OnQuestObjectiveComplete.RemoveDynamic(this, &UQuestStep::OnObjectiveComplete);
	LiveObjective->OnQuestObjectiveProgress.RemoveDynamic(this, &UQuestStep::OnObjectiveProgress);
	LiveObjective->OnQuestObjectiveRefused.RemoveDynamic(this, &UQuestStep::OnObjectiveRefused);
	LiveObjective->OnQuestObjectiveTriggerDeactivation.RemoveDynamic(this, &UQuestStep::OnObjectiveTriggerDeactivation);
	LiveObjective->OnQuestObjectiveTriggerSatisfied.RemoveDynamic(this, &UQuestStep::OnObjectiveTriggerSatisfied);
	LiveObjective->MarkReleased();

	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestStep::ReleaseLiveObjective : '%s' released '%s'"),
		*GetContextualTag().ToString(), *LiveObjective->GetName());

	LiveObjective = nullptr;
}

void UQuestStep::ScheduleCompletedObjectiveRelease()
{
	if (!LiveObjective) return;

	UWorld* World = GetWorld();
	if (!World)
	{
		// Nothing to schedule against, and no frame for post-completion work to run in either. Release now.
		ReleaseLiveObjective();
		return;
	}

	// End of the completing frame, not the moment of completion: the Blueprint chain that called Complete is still
	// running, and so is the manager's entire resolution cascade. The world's timer manager ticks after every actor
	// tick group, so a completion raised from gameplay releases late in that SAME frame; one raised after that point
	// (a tickable object, a latent resume) releases on the next. Both land after the synchronous chain, which is the
	// guarantee. The timer is world-owned, so PIE teardown drops it for free; a paused game defers to the unpause.
	TWeakObjectPtr<UQuestStep> WeakStep(this);
	TWeakObjectPtr<UQuestObjective> WeakObjective(LiveObjective);
	World->GetTimerManager().SetTimerForNextTick([WeakStep, WeakObjective]()
	{
		UQuestStep* Step = WeakStep.Get();

		// Release ONLY the objective that completed. A re-activation inside the same frame installs a replacement,
		// and releasing that one would unbind a step that has only just started.
		if (Step && WeakObjective.IsValid() && Step->LiveObjective == WeakObjective.Get())
		{
			Step->ReleaseLiveObjective();
		}
	});
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
