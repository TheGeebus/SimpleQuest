// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeBase.h"
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

	FORCEINLINE TSoftClassPtr<UQuestObjective> GetQuestObjective() const { return QuestObjective; }
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE TSubclassOf<AActor> GetTargetClass() const { return TargetClass; }
	FORCEINLINE int32 GetNumberOfElements() const { return NumberOfElements; }
	FORCEINLINE FVector GetTargetVector() const { return TargetVector; }
	FORCEINLINE UQuestObjective* GetActiveObjective() const { return ActiveObjective; }

protected:
	virtual void ActivateInternal(FGameplayTag InContextualTag) override;

	/** The objective that defines how this step is completed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftClassPtr<UQuestObjective> QuestObjective;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TSet<TSoftObjectPtr<AActor>> TargetActors;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TSubclassOf<AActor> TargetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	int32 NumberOfElements = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FVector TargetVector = FVector::ZeroVector;

private:
	UPROPERTY()
	TObjectPtr<UQuestObjective> ActiveObjective;
	
	UFUNCTION(BlueprintCallable)
	void OnObjectiveComplete(FGameplayTag OutcomeTag);
};
