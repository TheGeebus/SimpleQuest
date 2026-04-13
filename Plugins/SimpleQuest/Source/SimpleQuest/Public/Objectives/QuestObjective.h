// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestObjective.generated.h"

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
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnableTarget, UObject*, InTargetObject, bool, bNewIsEnabled);
	FOnEnableTarget OnEnableTarget;
		
	DECLARE_DELEGATE_TwoParams(FSetCounterDelegate, int32, int32);
	FSetCounterDelegate OnTargetTriggered;
	
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQuestObjectiveComplete, FGameplayTag, OutcomeTag);
	FQuestObjectiveComplete OnQuestObjectiveComplete;
	
	/**
	 * Set the initial conditions for the quest step. This event may be overridden to provide a convenient place
	 * to bind additional delegates. (see: UGoToQuestObjective)
	 * 
	 * @param InTargetActors a set of specific target actors in the scene
	 * @param InTargetClasses a set of classes to target (as for kills or pickups)
	 * @param NumElementsRequired the number of elements required to complete the step
	 * @param bUseCounter use a quest counter widget to track the status of this step
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void SetObjectiveTarget(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, const TSet<TSubclassOf<AActor>>& InTargetClasses, int32 NumElementsRequired = 0, bool bUseCounter = false);
	
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
	//UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	//bool IsObjectRelevant(UObject* InTargetObject);
	
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

	/**
	 * Override and return any native gameplay tags that should appear as Outcome pins on a Quest Step node hosting
	 * this objective. This is one of three additive methods to define the possible outcomes for an objective.
	 *
	 * - Refer to UQuestObjective::PossibleOutcomes for additional documentation.
	 *
	 * @see UQuestObjective::PossibleOutcomes
	 */
	virtual TArray<FGameplayTag> GetPossibleOutcomes() const;
	
protected:

	/**
	 * Outcome Tag Discovery																						<br>
	 * ---------------------																						<br>
	 * The editor discovers outcome tags from three additive sources. All results merge
	 * into a single deduplicated set — pins on the Step node reflect the union.
	 *
	 * 1. K2 Node Scan (Blueprint subclasses):																		
	 *    - Place UK2Node_CompleteObjectiveWithOutcome nodes in event graphs.							
	 *    - Each node's OutcomeTag is discovered automatically.
	 *
	 * 2. UPROPERTY Reflection (C++ subclasses):																	
	 *    - Declare FGameplayTag properties with the ObjectiveOutcome metadata specifier.				
	 *    - Back each tag with UE_DEFINE_GAMEPLAY_TAG to guarantee registration before CDO
	 *      construction. Bare RequestGameplayTag in a constructor will fail if the tag
	 *      is not yet loaded from INI at module init time.
	 *    - The editor discovers tagged properties via TFieldIterator reflection scan.
	 *																									
	 *        - In the header:																						<br>
	 *			   UPROPERTY(EditDefaultsOnly, meta = (Categories = "Quest.Outcome", ObjectiveOutcome))				<br>
	 *			   FGameplayTag Outcome_Reached;	
	 *																									
	 *        - In the .cpp (file scope):																			<br>
	 *			   UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_Reached, "Quest.Outcome.Reached")
	 *
	 *        - In the constructor:																					<br>
	 *			   Outcome_Reached = Tag_Outcome_Reached;
	 *
	 *        - In TryCompleteObjective:																			<br>
	 *			   CompleteObjectiveWithOutcome(Outcome_Reached);											
	 *																									
	 *    - Categories="Quest.Outcome" filters the tag picker to the outcome namespace.					
	 *    - ObjectiveOutcome marks the property for discovery — no value needed,
	 *      presence is sufficient.
	 *
	 * 3. Virtual GetPossibleOutcomes (programmatic / legacy):
	 *    - Override GetPossibleOutcomes() to return outcome tags computed at CDO construction time.
	 *    - Use this for dynamic or configuration-driven outcomes that cannot be expressed as
	 *      individual UPROPERTY members.																
	 *    - Tags returned here are not constrained to the Quest.Outcome namespace.
	 *
	 * All three sources are additive across the inheritance chain. A Blueprint subclass of a
	 * C++ class that declares ObjectiveOutcome properties and overrides GetPossibleOutcomes()
	 * will produce pins for all three sets combined.
	 *
	 * @see USimpleQuestEditorUtilities::DiscoverObjectiveOutcomes
	 * @see UK2Node_CompleteObjectiveWithOutcome
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "Quest"), Category = "Outcomes")
	TArray<FGameplayTag> PossibleOutcomes;
	
	UFUNCTION(BlueprintCallable)
	void CompleteObjectiveWithOutcome(FGameplayTag OutcomeTag);

	UFUNCTION(BlueprintCallable)
	void EnableTargetObject(UObject* Target, bool bIsTargetEnabled) const;

	UFUNCTION(BlueprintCallable)
	void EnableQuestTargetActors(bool bIsTargetEnabled);

	UFUNCTION(BlueprintCallable)
	void EnableQuestTargetClasses(bool bIsTargetEnabled) const;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSoftObjectPtr<AActor>> TargetActors;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSubclassOf<AActor>> TargetClasses;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	int32 MaxElements = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	int32 CurrentElements = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true))
	bool bStepCompleted = false;
	UPROPERTY()
	bool bUseQuestCounter = false;
	
public:
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE const TSet<TSubclassOf<AActor>>& GetTargetClasses() const { return TargetClasses; }
	FORCEINLINE int32 GetMaxElements() const { return MaxElements; }
	// Broadcasts OnSetCounter when changing the value 
	UFUNCTION(BlueprintCallable, BlueprintSetter=SetCurrentElements)
	void SetCurrentElements(const int32 NewAmount);
	FORCEINLINE int32 GetCurrentElements() const { return CurrentElements; }
};
