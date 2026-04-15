// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeBase.h"
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
	virtual void Activate(FGameplayTag InContextualTag) override;

protected:
	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
	virtual void DeactivateInternal(FGameplayTag InContextualTag) override;

	/** The objective that defines how this step is completed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftClassPtr<UQuestObjective> QuestObjective;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TSet<TSoftObjectPtr<AActor>> TargetActors;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TSet<TSubclassOf<AActor>> TargetClasses;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	int32 NumberOfElements = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FVector TargetVector = FVector::ZeroVector;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	EPrerequisiteGateMode PrerequisiteGateMode = EPrerequisiteGateMode::GatesProgression;
	
private:
	UPROPERTY()
	TObjectPtr<UQuestObjective> ActiveObjective;

	/** Completion payload captured from the objective before teardown. Read by the manager during context assembly. */
	FQuestObjectiveContext CompletionData;

	UFUNCTION(BlueprintCallable)
	void OnObjectiveComplete(FGameplayTag OutcomeTag);

public:
	FORCEINLINE TSoftClassPtr<UQuestObjective> GetQuestObjective() const { return QuestObjective; }
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE const TSet<TSubclassOf<AActor>>& GetTargetClasses() const { return TargetClasses; }
	FORCEINLINE int32 GetNumberOfElements() const { return NumberOfElements; }
	FORCEINLINE FVector GetTargetVector() const { return TargetVector; }
	FORCEINLINE UQuestObjective* GetActiveObjective() const { return ActiveObjective; }
	FORCEINLINE EPrerequisiteGateMode GetPrerequisiteGateMode() const { return PrerequisiteGateMode; }
	FORCEINLINE const FQuestObjectiveContext& GetCompletionData() const { return CompletionData; }
	
};
