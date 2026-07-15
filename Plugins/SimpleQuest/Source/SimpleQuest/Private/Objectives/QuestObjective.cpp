// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#include "Objectives/QuestObjective.h"
#include "GameplayTagContainer.h"
#include "SimpleQuestLog.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "Subsystems/QuestStateSubsystem.h"


void UQuestObjective::TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& InContext)
{
	/*----------------------------------------------------------------------------------------------------------------*
	 * Set fields on an FQuestObjectiveTriggerContext and pass it to CompleteObjectiveWithOutcome.
	 * Common fields:
	 *   InContext can be forwarded directly for pass-through, or build a new one:
	 *   FQuestObjectiveTriggerContext OutContext;
	 *   OutContext.TriggeredActor = InContext.TriggeredActor;
	 *   OutContext.Instigator = InContext.Instigator;
	 * Game-specific extension - any desired struct type, such as example user-defined struct FMyKillData:
	 *   OutContext.CustomData = FInstancedStruct::Make<FMyKillData>(Target->GetFName(), DamageType, ...);
	 *----------------------------------------------------------------------------------------------------------------*/

	UE_LOG(LogSimpleQuestActivation, Warning, TEXT("Called parent UQuestObjective::TryCompleteObjective. Override this event to provide quest completion logic."));
}

void UQuestObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveAuthoredConfig& Authored, const FQuestObjectiveRuntimeContext& Runtime)
{
	// Default composition: authored target classes; authored actors unioned with the caller's runtime actors.
	// Subclasses override to compose differently — they have both halves with full provenance.
	TargetClasses = Authored.TargetClasses;
	TargetActors = Authored.TargetActors;
	TargetActors.Append(Runtime.IncomingContext.Config.TargetActors);
}

void UQuestObjective::DispatchOnObjectiveActivated(const FQuestObjectiveAuthoredConfig& Authored, const FQuestObjectiveRuntimeContext& Runtime, FGameplayTag InOwningStepTag)
{
	OwningStepTag = InOwningStepTag;
	OnObjectiveActivated(Authored, Runtime);
}

void UQuestObjective::DispatchTryCompleteObjective(const FQuestObjectiveTriggerContext& InContext)
{
	LastTriggerContext = InContext;
	TryCompleteObjective(InContext);
}

FQuestObjectiveTriggerContext UQuestObjective::ResolveTriggerContext(const FQuestObjectiveTriggerContext& AdopterContext) const
{
	FQuestObjectiveTriggerContext Out = AdopterContext;
	if (!Out.TriggeredActor) Out.TriggeredActor = LastTriggerContext.TriggeredActor;
	if (!Out.Instigator.IsValid()) Out.Instigator = LastTriggerContext.Instigator;
	if (!Out.CustomData.IsValid()) Out.CustomData = LastTriggerContext.CustomData;
	if (!Out.CustomTag.IsValid()) Out.CustomTag = LastTriggerContext.CustomTag;
	Out.OriginatingTriggerComponent = LastTriggerContext.OriginatingTriggerComponent;
	return Out;
}

void UQuestObjective::DispatchOnObjectiveDeactivated()
{
	OnObjectiveDeactivated();
	// Clear cached identity after the subclass override has had its last chance to read it — keeps GetOwningStepTag()
	// honest after the Step releases this Objective. The Step also calls UnregisterActiveObjective on the QSS
	// (see UQuestStep::DeactivateInternal / OnObjectiveComplete) so the live-objective registry stays in sync.
	OwningStepTag = FGameplayTag();
}

void UQuestObjective::OnObjectiveDeactivated_Implementation()
{
	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestObjective::OnObjectiveDeactivated_Implementation — base no-op. Override in "
		"subclass to unsubscribe from external event sources, tear down UI handles, release timers, etc. (%s)"), *GetFullName());
}

TArray<FGameplayTag> UQuestObjective::GetPossibleOutcomes() const
{
	return {};
}

void UQuestObjective::CompleteObjectiveWithOutcome(FGameplayTag OutcomeTag, FName PathIdentity, const FQuestObjectiveTriggerContext& InCompletionContext, const FQuestObjectiveActivationContext& InForwardParams)
{
	const FQuestObjectiveTriggerContext Effective = ResolveTriggerContext(InCompletionContext);
	CompletionContext = Effective;
	ForwardActivationParams = InForwardParams;
	
	// Carry the completer's attribution downstream when the caller supplied no forward params of its own, so rewards
	// (and any downstream node) see who/what completed this. Lineage (Origin*) is set by the chain, not here.
	if (!ForwardActivationParams.Instigator.IsValid()) ForwardActivationParams.Instigator = Effective.Instigator;
	if (!ForwardActivationParams.CustomData.IsValid()) ForwardActivationParams.CustomData = Effective.CustomData;
	if (!ForwardActivationParams.CustomTag.IsValid())  ForwardActivationParams.CustomTag  = Effective.CustomTag;
	
	// Auto-derive PathIdentity from OutcomeTag.GetTagName() when caller didn't supply one explicitly. Static K2
	// placements supply NAME_None and depend on this fallback for back-compat; dynamic K2 placements supply an
	// explicit PathIdentity from the node's authored PathName.
	const FName ResolvedPath = PathIdentity.IsNone() ? OutcomeTag.GetTagName() : PathIdentity;
	OnQuestObjectiveComplete.Broadcast(OutcomeTag, ResolvedPath);
	ConditionalBeginDestroy();
}

void UQuestObjective::ReportProgress(const FQuestObjectiveTriggerContext& ProgressContext)
{
	const FQuestObjectiveTriggerContext Effective = ResolveTriggerContext(ProgressContext);
	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ReportProgress: %d/%d — %s"), Effective.CurrentCount, Effective.RequiredCount, *GetFullName());
	OnQuestObjectiveProgress.Broadcast(Effective);
}

void UQuestObjective::RefuseTrigger(FGameplayTag RefusalReason, const FQuestObjectiveTriggerContext& TriggerContext)
{
	const FQuestObjectiveTriggerContext Effective = ResolveTriggerContext(TriggerContext);
	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestObjective::RefuseTrigger : Reason=%s, TriggeredActor=%s — %s"),
		*RefusalReason.ToString(),
		Effective.TriggeredActor ? *Effective.TriggeredActor->GetName() : TEXT("(none)"),
		*GetFullName());
	OnQuestObjectiveRefused.Broadcast(RefusalReason, Effective);
}

void UQuestObjective::PublishTriggerDeactivation(FGameplayTag OutcomeTag, const FQuestObjectiveTriggerContext& FinalContext)
{
	const FQuestObjectiveTriggerContext Effective = ResolveTriggerContext(FinalContext);
	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestObjective::PublishTriggerDeactivation : Outcome=%s — %s"),
		*OutcomeTag.ToString(),
		*GetFullName());
	OnQuestObjectiveTriggerDeactivation.Broadcast(OutcomeTag, Effective);
}

void UQuestObjective::PublishTriggerSatisfied(const FQuestObjectiveTriggerContext& TriggerContext)
{
	const FQuestObjectiveTriggerContext Effective = ResolveTriggerContext(TriggerContext);
	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestObjective::PublishTriggerSatisfied : actor=%s — %s"),
		Effective.TriggeredActor ? *Effective.TriggeredActor->GetName() : TEXT("null"),
		*GetFullName());
	OnQuestObjectiveTriggerSatisfied.Broadcast(Effective);
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
			UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestObjective::EnableQuestTargetActor : enabling target actor: %s"), *TargetActor->GetFName().ToString());
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

TArray<FGameplayTag> UQuestObjective::GetOwningStepAliasTags() const
{
	if (!OwningStepTag.IsValid()) return {};
	const UWorld* World = GetWorld();
	if (!World) return {};
	const UGameInstance* GI = World->GetGameInstance();
	if (!GI) return {};
	const UQuestStateSubsystem* QSS = GI->GetSubsystem<UQuestStateSubsystem>();
	if (!QSS) return {};
	return QSS->GetAssetScopedAliasTagsForCanonical(OwningStepTag);
}

TArray<FQuestRoleSourceInfo> UQuestObjective::GetTriggersTargetingThisStep() const
{
	if (!OwningStepTag.IsValid()) return {};
	const UWorld* World = GetWorld();
	if (!World) return {};
	const UGameInstance* GI = World->GetGameInstance();
	if (!GI) return {};
	const UQuestStateSubsystem* QSS = GI->GetSubsystem<UQuestStateSubsystem>();
	return QSS ? QSS->GetActiveTriggersForTag(OwningStepTag) : TArray<FQuestRoleSourceInfo>{};
}

TArray<FQuestRoleSourceInfo> UQuestObjective::GetGiversTargetingThisStep() const
{
	if (!OwningStepTag.IsValid()) return {};
	const UWorld* World = GetWorld();
	if (!World) return {};
	const UGameInstance* GI = World->GetGameInstance();
	if (!GI) return {};
	const UQuestStateSubsystem* QSS = GI->GetSubsystem<UQuestStateSubsystem>();
	return QSS ? QSS->GetActiveGiversForTag(OwningStepTag) : TArray<FQuestRoleSourceInfo>{};
}

