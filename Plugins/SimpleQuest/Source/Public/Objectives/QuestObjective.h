// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QuestObjective.generated.h"

class UQuestSignalSubsystem;
class UQuestTargetInterface;
class IQuestTargetInterface;

/**
 * Base class with functions intended to be overridden to provide the logic for the completion of a given quest step.
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
	 * This event is intended to be overridden by child classes to provide the logic for quest step completion.
	 * It should be called each time the player interacts with a potential quest target. When conditions for
	 * this quest step are fulfilled - either succeeding or failing - this event should call
	 * CompleteObjective to advance the quest.
	 *
	 * Example child objectives: UGoToQuestObjective and UKillClassQuestObjective
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void TryCompleteObjective(UObject* InTargetObject);

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

	
protected:
	UFUNCTION(BlueprintCallable)
	void CompleteObjective(bool bDidSucceed);

	UFUNCTION(BlueprintCallable)
	void EnableTargetObject(UObject* Target, bool bIsTargetEnabled) const;

	UFUNCTION(BlueprintCallable)
	void EnableQuestTargetActorSet(bool bIsTargetEnabled);

	UFUNCTION(BlueprintCallable)
	void EnableQuestTargetClass(bool bIsTargetEnabled) const;

	//UPROPERTY()
	//TObjectPtr<UQuestSignalSubsystem> QuestSignalSubsystem;	
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
	// TSet<FScriptInterface> QuestTargetInterfaces;
	
public:
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE UClass* GetTargetClass() const { return TargetClass; }
	FORCEINLINE int32 GetMaxElements() const { return MaxElements; }
	FORCEINLINE int32 GetStepID() const { return StepID; }
	// Broadcasts OnSetCounter when changing the value 
	UFUNCTION(BlueprintCallable, BlueprintSetter=SetCurrentElements)
	void SetCurrentElements(const int32 NewAmount);
	FORCEINLINE int32 GetCurrentElements() const { return CurrentElements; }
	// void SetQuestSignalSubsystem(UQuestSignalSubsystem* InQuestSignalSubsystem);
};
