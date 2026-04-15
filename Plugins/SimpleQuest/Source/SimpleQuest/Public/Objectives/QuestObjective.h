// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "StructUtils/InstancedStruct.h"
#include "QuestObjective.generated.h"

class UQuestTargetInterface;
class IQuestTargetInterface;

/**
 * Base class with functions intended to be overridden to provide logic for the completion of a given quest step.
 *
 * Outcome tags are discovered automatically by the editor from two primary sources on subclasses:
 * K2 node scan (Blueprint) and UPROPERTY reflection (C++). A virtual fallback (GetPossibleOutcomes)
 * is available for programmatic or dynamic cases. See GetPossibleOutcomes for full documentation.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuestObjective : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnableTarget, UObject*, InTargetObject, bool, bNewIsEnabled);
	FOnEnableTarget OnEnableTarget;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQuestObjectiveComplete, FGameplayTag, OutcomeTag);
	FQuestObjectiveComplete OnQuestObjectiveComplete;
	
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestObjectiveProgress, FQuestObjectiveContext, ProgressData);
	FOnQuestObjectiveProgress OnQuestObjectiveProgress;
	
	/**
	 * Set the initial conditions for the quest step. This event may be overridden to provide a convenient place
	 * to bind additional delegates. (see: UGoToQuestObjective)
	 * 
	 * @param InTargetActors a set of specific target actors in the scene
	 * @param InTargetClasses a set of classes to target (as for kills or pickups)
	 * @param NumElementsRequired the number of elements required to complete the step
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void SetObjectiveTarget(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, const TSet<TSubclassOf<AActor>>& InTargetClasses, int32 NumElementsRequired = 0);
	
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
	 * @param InContext the context of this trigger event containing the triggered actor, any relevant instigator, and an
	 *					optional designer-defined instanced struct that may contain additional fields as needed.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void TryCompleteObjective(const FQuestObjectiveContext& InContext);

	/**
	 * Outcome Tag Discovery																						<br>
	 * ---------------------																						<br>
	 * The editor discovers outcome tags from two primary sources. All results merge
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
	 * Both sources are additive across the inheritance chain. A Blueprint subclass of a
	 * C++ class that declares ObjectiveOutcome properties will produce pins for both sets combined.
	 *
	 * Override this virtual as a fallback for programmatic or dynamic outcomes that cannot be
	 * expressed as individual UPROPERTY members or K2 nodes — e.g. configuration-driven outcomes
	 * computed at CDO construction time. Tags returned here are not constrained to the
	 * Quest.Outcome namespace. Base implementation returns an empty array.
	 *
	 * @see USimpleQuestEditorUtilities::DiscoverObjectiveOutcomes
	 * @see UK2Node_CompleteObjectiveWithOutcome
	 */
	virtual TArray<FGameplayTag> GetPossibleOutcomes() const;
	
protected:	
	UFUNCTION(BlueprintCallable)
	void CompleteObjectiveWithOutcome(FGameplayTag OutcomeTag, const FQuestObjectiveContext& InCompletionData);

	/**
	 * Fires OnQuestObjectiveProgress. Step forwards to manager, which publishes FQuestProgressEvent on the step tag channel.
	 * Use this directly for objectives with custom progress logic (multi-counter, phase-based, etc.).
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void ReportProgress(const FQuestObjectiveContext& InProgressData);
	
	UFUNCTION(BlueprintCallable)
	void EnableTargetObject(UObject* Target, bool bIsTargetEnabled) const;

	UFUNCTION(BlueprintCallable)
	void EnableQuestTargetActors(bool bIsTargetEnabled);

	UFUNCTION(BlueprintCallable)
	void EnableQuestTargetClasses(bool bIsTargetEnabled) const;
	
private:
	/** Set by CompleteObjectiveWithOutcome. Read by the step via TakeCompletionData. */
	UPROPERTY()
	FQuestObjectiveContext CompletionData;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSoftObjectPtr<AActor>> TargetActors;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSubclassOf<AActor>> TargetClasses;
	
public:
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE const TSet<TSubclassOf<AActor>>& GetTargetClasses() const { return TargetClasses; }
	FQuestObjectiveContext TakeCompletionData() { return MoveTemp(CompletionData); }
};
