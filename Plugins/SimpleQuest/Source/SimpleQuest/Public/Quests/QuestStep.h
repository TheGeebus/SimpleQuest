// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeBase.h"
#include "Types/QuestObjectiveActivationParams.h"
#include "Types/QuestObjectiveContext.h"
#include "Types/QuestStepEnums.h"
#include "QuestStep.generated.h"

class UQuestObjective;

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
	DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnNodeProgress, UQuestStep*, Step, FQuestObjectiveContext, ProgressData);
	FOnNodeProgress OnNodeProgress;
	
	virtual void Activate(FGameplayTag InContextualTag) override;

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

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	EPrerequisiteGateMode PrerequisiteGateMode = EPrerequisiteGateMode::GatesProgression;

public:
	/**
	 * Snapshot of the final composed params delivered to the objective at activation. Retained for Piece D chain
	 * propagation — ChainToNextNodes reads OriginChain to build the forwarded chain for downstream steps. Populated
	 * in ActivateInternal; cleared in DeactivateInternal.
	 */
	UPROPERTY(Transient)
	FQuestObjectiveActivationParams ReceivedActivationParams;

	/**
	 * Populated from the objective's forward-params at completion. Read by ChainToNextNodes to pre-stamp
	 * downstream PendingActivationParams before activation. Transient across step activations.
	 */
	UPROPERTY(Transient)
	FQuestObjectiveActivationParams CompletionForwardParams;
	
private:
	UPROPERTY()
	TObjectPtr<UQuestObjective> ActiveObjective;

	/** Completion payload captured from the objective before teardown. Read by the manager during context assembly. */
	FQuestObjectiveContext CompletionData;

	UFUNCTION(BlueprintCallable)
	void OnObjectiveComplete(FGameplayTag OutcomeTag);

	UFUNCTION()
	void OnObjectiveProgress(FQuestObjectiveContext ProgressData);

public:
	FORCEINLINE TSoftClassPtr<UQuestObjective> GetQuestObjective() const { return QuestObjective; }
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE const TSet<TSoftClassPtr<AActor>>& GetTargetClasses() const { return TargetClasses; }
	FORCEINLINE int32 GetNumberOfElements() const { return NumberOfElements; }
	FORCEINLINE UQuestObjective* GetActiveObjective() const { return ActiveObjective; }
	FORCEINLINE EPrerequisiteGateMode GetPrerequisiteGateMode() const { return PrerequisiteGateMode; }
	FORCEINLINE const FQuestObjectiveContext& GetCompletionData() const { return CompletionData; }
	FORCEINLINE const FQuestObjectiveActivationParams& GetReceivedActivationParams() const { return ReceivedActivationParams; }
	FORCEINLINE const FQuestObjectiveActivationParams& GetCompletionForwardParams() const { return CompletionForwardParams; }	
};
