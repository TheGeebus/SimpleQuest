// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeBase.h"
#include "Types/QuestObjectiveActivationContext.h"
#include "Types/QuestObjectiveTriggerContext.h"
#include "Types/QuestStepEnums.h"
#include "QuestStep.generated.h"

class UQuestObjective;
class UQuestObjectiveConfig;

/**
 * Concrete leaf node. Hosts a single UQuestObjective and the target data required to fulfil it. Replaces FQuestStep
 * once the unified graph model is fully in place.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuestStep : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler; 

public:
	DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnNodeProgress, UQuestStep*, Step, FQuestObjectiveTriggerContext, ProgressData);
	FOnNodeProgress OnNodeProgress;

	DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnNodeRefused, UQuestStep*, Step, FGameplayTag, RefusalReason, FQuestObjectiveTriggerContext, TriggerContext);
		
	/**
	 * Fired by OnObjectiveRefused (forwarded from the live objective's OnQuestObjectiveRefused broadcast). Manager binds
	 * and publishes FQuestTriggerResponseEvent(Refused) on the step's tag channel.
	 */
	FOnNodeRefused OnNodeRefused;

	DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnNodeTriggerDeactivation, UQuestStep*, Step, FGameplayTag, OutcomeTag, FQuestObjectiveTriggerContext, FinalContext);
	
	/**
	 * Fired by OnObjectiveTriggerDeactivation (forwarded from the live objective's OnQuestObjectiveTriggerDeactivation
	 * broadcast - manual fires only; the Completed / Interrupted auto-publishes happen manager-side adjacent to
	 * FQuestEndedEvent / FQuestDeactivatedEvent). Manager binds and publishes FQuestTriggerDeactivatedEvent(Manual).
	 */
	FOnNodeTriggerDeactivation OnNodeTriggerDeactivation;

	DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnNodeTriggerSatisfied, UQuestStep*, Step, FQuestObjectiveTriggerContext, Context);

	/**
	 * Bound by the manager. Forwards the Objective's per-actor satisfaction signal so the manager publishes
	 * FQuestTriggerSatisfiedEvent on the step's tag channel for Trigger Components to filter and react to.
	 */
	FOnNodeTriggerSatisfied OnNodeTriggerSatisfied;
	
	virtual void Activate(FGameplayTag InContextualTag) override;

	virtual bool IsStepNode() const override { return true; }
	
	/**
	 * Save-restore replay. Rebuilds the live objective from a saved activation snapshot WITHOUT firing the lifecycle
	 * (no OnNodeStarted, so no SetQuestLive / lifecycle events / entry record - those fired at the original start and
	 * were restored in bulk). Stamps EQuestActivationProvenance::Restored on the runtime context so the objective can
	 * distinguish a load from a fresh start. Called by UQuestManagerSubsystem::RestoreQuestlineGraph for each Step the
	 * restored WorldState marks Live. IncomingContext is the caller's saved runtime input (FQuestEntryArrival::
	 * ActivationContextSnapshot); the Step's authored config re-derives here from its own UPROPERTYs.
	 */
	void RestoreObjective(const FQuestObjectiveActivationContext& IncomingContext, FGameplayTag InContextualTag);

protected:
	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
	virtual void DeactivateInternal(FGameplayTag InContextualTag) override;
	virtual void ResetTransientState() override;
	
	/** The objective that defines how this step is completed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftClassPtr<UQuestObjective> QuestObjective;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TSet<TSoftObjectPtr<AActor>> TargetActors;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TSet<TSoftClassPtr<AActor>> TargetClasses;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	int32 NumberOfElements = 0;

	/**
	 * Designer-attached "blank slate" config for this Step's objective - the authored-side analog of the runtime
	 * CustomData instanced struct. Point it at a UQuestObjectiveConfig subclass asset holding whatever typed
	 * configuration the objective wants; the objective casts it on read. The picker filters to UQuestObjectiveConfig
	 * and its descendants.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TSoftObjectPtr<UQuestObjectiveConfig> ConfigAsset;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	EPrerequisiteGateMode PrerequisiteGateMode = EPrerequisiteGateMode::GatesProgression;

	/**
	 * Container chain ordered innermost → outermost. Compile-time populated by FQuestlineGraphCompiler::
	 * ComputeContainerReachability. Read by Step state-transition methods (SetQuestLive / SetQuestResolved /
	 * SetQuestDeactivated) to walk up and re-derive each ancestor container's Live state.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TArray<FGameplayTag> AncestorContainerTags;

	
public:
	/**
	 * The runtime half of the activation context handed to the objective - the caller's IncomingContext plus the
	 * framework-stamped Provenance and IncomingOutcomeTag. Set in ActivateInternal before OnNodeStarted fires, so
	 * HandleOnNodeStarted's RecordEntry captures a populated snapshot for the registry's start record. Also drives
	 * chain propagation: ChainToNextNodes reads IncomingContext.OriginChain to extend the forwarded chain for
	 * downstream steps. Cleared in DeactivateInternal.
	 */
	UPROPERTY(Transient)
	FQuestObjectiveRuntimeContext ReceivedActivationContext;

	/**
	 * Populated from the objective's forward-params at completion. Read by ChainToNextNodes to pre-stamp
	 * downstream PendingActivationContext before activation. Transient across step activations.
	 */
	UPROPERTY(Transient)
	FQuestObjectiveActivationContext CompletionForwardParams;
	
private:
	UPROPERTY()
	TObjectPtr<UQuestObjective> LiveObjective;

	/** Completion payload captured from the objective before teardown. Read by the manager during context assembly. */
	FQuestObjectiveTriggerContext CompletionContext;

	UFUNCTION()
	void OnObjectiveComplete(FGameplayTag OutcomeTag, FName PathIdentity);

	UFUNCTION()
	void OnObjectiveProgress(FQuestObjectiveTriggerContext ProgressContext);
	
	UFUNCTION()
	void OnObjectiveRefused(FGameplayTag RefusalReason, FQuestObjectiveTriggerContext TriggerContext);

	UFUNCTION()
	void OnObjectiveTriggerDeactivation(FGameplayTag OutcomeTag, FQuestObjectiveTriggerContext FinalContext);

	UFUNCTION()
	void OnObjectiveTriggerSatisfied(FQuestObjectiveTriggerContext Context);

	static void UnregisterObjectiveFromQuestStateSubsystem(UQuestObjective* Objective, const UWorld* World);
	
	/** Packs this Step's authored UPROPERTYs into the objective's authored-config half. Identical for fresh activation and save restore. */
	FQuestObjectiveAuthoredConfig BuildAuthoredConfig() const;

	/**
	 * Instantiates LiveObjective, wires its delegates, registers it with the state subsystem, and dispatches
	 * OnObjectiveActivated. The instantiation half of activation with no lifecycle side effects - shared by the normal
	 * ActivateInternal path and the save-restore RestoreObjective path.
	 */
	void InstantiateLiveObjective(const FQuestObjectiveAuthoredConfig& Authored, const FQuestObjectiveRuntimeContext& Runtime, FGameplayTag InContextualTag);

	/**
	 * Unbinds this Step from LiveObjective, marks the objective released, and drops the reference. The single place the
	 * Step lets go of an objective: at the end of the frame a completion happened in, immediately on interruption, and
	 * defensively if a new activation arrives while an old objective is still held.
	 */
	void ReleaseLiveObjective();

	/**
	 * Queues ReleaseLiveObjective for the next tick, so the completing frame's execution chain - the Blueprint work
	 * placed after a Complete Objective node, and the manager's whole resolution cascade - runs with this Step still
	 * listening. Releases immediately when there is no world to schedule against.
	 */
	void ScheduleCompletedObjectiveRelease();
	
public:
	FORCEINLINE TSoftClassPtr<UQuestObjective> GetQuestObjective() const { return QuestObjective; }
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE const TSet<TSoftClassPtr<AActor>>& GetTargetClasses() const { return TargetClasses; }
	FORCEINLINE int32 GetNumberOfElements() const { return NumberOfElements; }
	FORCEINLINE TSoftObjectPtr<UQuestObjectiveConfig> GetConfigAsset() const { return ConfigAsset; }
	FORCEINLINE UQuestObjective* GetLiveObjective() const { return LiveObjective; }
	FORCEINLINE EPrerequisiteGateMode GetPrerequisiteGateMode() const { return PrerequisiteGateMode; }
	FORCEINLINE const FQuestObjectiveTriggerContext& GetCompletionContext() const { return CompletionContext; }
	FORCEINLINE const FQuestObjectiveRuntimeContext& GetReceivedActivationParams() const { return ReceivedActivationContext; }
	FORCEINLINE const FQuestObjectiveActivationContext& GetCompletionForwardParams() const { return CompletionForwardParams; }
	FORCEINLINE const TArray<FGameplayTag>& GetAncestorContainerTags() const { return AncestorContainerTags; }
};
