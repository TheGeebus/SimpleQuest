// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QuestObjective.generated.h"

class UQuestSignalSubsystem;
class UQuestTargetInterface;
class IQuestTargetInterface;

/**
 * Base class with functions intended to be overridden to provide logic for the completion of a given quest step.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuestObjective : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnableTarget, UObject*, InTargetObject, int32, InStepID, bool, bNewIsEnabled);
	FOnEnableTarget OnEnableTarget;
		
	DECLARE_DELEGATE_TwoParams(FSetCounterDelegate, int32, int32);
	FSetCounterDelegate OnTargetTriggered;
	
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FQuestObjectiveComplete, int32, InStepID, bool, bDidSucceed);
	FQuestObjectiveComplete OnQuestObjectiveComplete;
	
	/**
	 * Set the initial conditions for the quest step. This event may be overridden to provide a convenient place
	 * to bind additional delegates. (see: UGoToQuestObjective)
	 * 
	 * @param InStepID numeric ID of the current quest step
	 * @param InTargetActors a set of specific target actors in the scene
	 * @param InTargetClass a generic class to target (as for kills or pickups)
	 * @param NumElementsRequired the number of elements required to complete the step
	 * @param bUseCounter use a quest counter widget to track the status of this step
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void SetObjectiveTarget(int32 InStepID, const TSet<TSoftObjectPtr<AActor>>& InTargetActors, UClass* InTargetClass = nullptr, int32 NumElementsRequired = 0, bool bUseCounter = false);

	/**
	 * Determine if the InTargetObject is relevant to the completion of this quest and logic should proceed to TryCompleteObjective.
	 * Should be overriden by child classes to define what objects are relevant to the completion of the objective through
	 * either success or failure. This allows quests to check for both relevancy and prerequisite completion when triggering
	 * a quest objective so that the system may signal that progress is still gated by another quest objective.
	 * 
	 * @param InTargetObject The quest target that was triggered. By default, this is checked against both the TargetClass and
	 * any TargetActors. Override this event to define custom conditions for relevancy.
	 * @return TRUE if the object is relevant to the completion of this quest objective, whether by success or failure.
	 * @see UQuestObjective::TryCompleteObjective()
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool IsObjectRelevant(UObject* InTargetObject);
	
	/**
	 * Count a relevant quest target and determine if the step should end in success or failure. This event is intended
	 * to be overridden by child classes to provide the logic for quest step completion. When a quest target is triggered,
	 * this will automatically be called if InTargetObject passes the relevancy check determined by the logic contained
	 * in the IsObjectRelevant override.
	 *
	 * This function should be used to count elements or perform additional logic after relevancy has been confirmed
	 * and call CompleteObjective to signal this objective has ended in either success or failure. Base class has no
	 * default implementation, but this can be overridden in C++ or Blueprint subclasses.
	 *
	 * Example child objectives: UGoToQuestObjective and UKillClassQuestObjective
	 * @param InTargetObject The quest target that was triggered. Will be checked against IsObjectRelevant prior to calling
	 * this function
 	 * @see UQuestObjective::IsObjectRelevant()
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void TryCompleteObjective(UObject* InTargetObject);
	
protected:
	UFUNCTION(BlueprintCallable)
	void CompleteObjective(bool bDidSucceed);

	UFUNCTION(BlueprintCallable)
	void EnableTargetObject(UObject* Target, bool bIsTargetEnabled) const;

	UFUNCTION(BlueprintCallable)
	void EnableQuestTargetActors(bool bIsTargetEnabled);

	UFUNCTION(BlueprintCallable)
	void EnableQuestTargetClass(bool bIsTargetEnabled) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSoftObjectPtr<AActor>> TargetActors;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TObjectPtr<UClass> TargetClass = nullptr;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	int32 MaxElements = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	int32 CurrentElements = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true))
	bool bStepCompleted = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true))
	int32 StepID = -1;
	UPROPERTY()
	bool bUseQuestCounter = false;
	
public:
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE UClass* GetTargetClass() const { return TargetClass; }
	FORCEINLINE int32 GetMaxElements() const { return MaxElements; }
	FORCEINLINE int32 GetStepID() const { return StepID; }
	// Broadcasts OnSetCounter when changing the value 
	UFUNCTION(BlueprintCallable, BlueprintSetter=SetCurrentElements)
	void SetCurrentElements(const int32 NewAmount);
	FORCEINLINE int32 GetCurrentElements() const { return CurrentElements; }
};
