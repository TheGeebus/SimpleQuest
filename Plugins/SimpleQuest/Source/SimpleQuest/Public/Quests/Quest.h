// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestTypes.h"
#include "GameplayTagContainer.h"
#include "Quests/QuestNode.h"
#include "Quest.generated.h"

class UQuest;
class UQuestReward;
class UQuestObjective;

/**
 * Each FQuestStep represents a simple goal - like a location or number of kills - that must be reached to progress a
 * given quest. Reusable logic for completing each step is defined by selecting or creating a new child of the
 * UQuestObjective base class and overriding its functionality.
 *
 * FQuestSteps may be linked to one another in non-linear fashion, and several can be active at once within the
 * same quest. An FQuestStep also contains all the information required to set both the QuestWidget and the CommsWidget
 * as needed.
 */
USTRUCT(BlueprintType)
struct FQuestStep
{
	GENERATED_USTRUCT_BODY()

	/**
    * The IDs of steps that must be completed before this one, if any. The step can still be activated before these steps
    * are finished but not completed. Disregarded for the first step in any quest - set PrerequisiteQuests instead.
    */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions)
	TSet<int32> PrerequisiteSteps = TSet<int32>();
	/**
	* Activate this quest step as soon as the quest is loaded. This will override the use of prerequisite quest steps
	* and instead force activation of the step upon first loading the quest. Unticking this will cause
	* the quest step to activate only when the step is explicitly started by another condition, i.e., by completion
	* of the preceding step. Meaningless for the first step in a quest.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions, AdvancedDisplay)
	bool bActivateOnQuestLoad = false;
	/**
	* If this property is enabled, all prerequisite steps must explicitly succeed for this step to begin. Steps that
	* end in either success or failure are all considered completed. This property controls whether they must be
	* completed successfully to progress.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions, AdvancedDisplay)
	bool bPrerequisitesMustSucceed = false;
	/**
	* Object containing the instructions for completing this step.
	* @see UQuestObjective
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions)
	TSubclassOf<UQuestObjective> QuestObjective = nullptr;
	/**
	* An optional integer used in determining quest completion, such as the number of elements that must be interacted
	* with to fulfill the conditions for this step.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions, AdvancedDisplay)
	int32 NumberOfElements = 0;
	/**
	* The quest step targets. A TSet of soft references to actors in the world, such as any specific targets that must be
	* interacted with to fulfill the conditions for completing this step, either in success or failure.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions)
	TSet<TSoftObjectPtr<AActor>> TargetActors;
	/**
	* A subclass of AActor. Usually a filter for objects that must be interacted with to fulfill conditions for
	* completing this quest step, such as a particular enemy type that must be killed or a certain item type that
	* must be picked up. This class reference is provided to the UQuestObjective, which can then use it in any
	* required context.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions)
	TSubclassOf<AActor> TargetClass;
	/**
	* An optional target vector. Could represent a target location, direction, or other three channel data type.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions)
	FVector TargetVector = FVector::ZeroVector;
	/**
	* The IDs of the next steps to attempt to start in the quest when this one is completed successfully. If this is
	* not set, completion of this step will simply advance the quest to the next step sequentially in the QuestSteps
	* array.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Completion)
	TSet<int32> NextSteps = TSet<int32>();
	/**
	* Automatically advance to the next quest step in the array of QuestSteps when this one is completed. Enabled by
	* default. Disabling will allow a step to function as a dead-end or as a prerequisite for another step without
	* triggering progression. Meaningless if the set of NextSteps contains any entries since they will always be loaded
	* on step completion or if this is the last step in the array - see: bQuestAutoComplete. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conditions, AdvancedDisplay)
	bool bAutoProgress = true;
	
	/**
	* Whether to update the QuestWidget when starting this step.
	* The QuestWidget includes a title, description, and optional counter.
	*
	* NOTE: This functionality should probably be moved to another class/system that subscribes to the appropriate
	* events.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = QuestWidget)
	bool bUpdateQuestWidget = false;
	/**
	* A struct containing the text for the QuestWidget. - Probably should be moved to another class.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = QuestWidget)
	FQuestText QuestWidgetText;
	/**
	* Whether to play a comms event when starting this step.
	*
	* NOTE: This functionality should probably be moved to another class/system that subscribes to the appropriate
	* events.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CommsEvent|Start")
	bool bUseStartEvent = false;
	/**
	* A struct containing the text for the CommsWidget during a starting event. - Probably should be moved to another class.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CommsEvent|Start")
	FCommsEvent StartEvent;
	/**
	* Whether to play a comms event when ending this step.
	*
	* NOTE: This functionality should probably be moved to another class/system that subscribes to the appropriate
	* events.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CommsEvent|End")
	bool bUseEndEvent = false;
	/**
	* A struct containing the text for the CommsWidget during an ending event. - Probably should be moved to another class.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CommsEvent|End")
	FCommsEvent EndEvent;
	/**
	* Whether this step has been completed. Some steps may be completed early by setting this value.
	* This allows them to be automatically progressed when loading.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StepEnd)
	bool bStepCompleted = false;
	/**
	* Overridable base class containing customizable quest reward data and logic. If this is set, a pointer to this
	* object is passed as part of the associated FQuestStepCompleted event. 
	* See also: UQuestReward
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StepEnd)
	TSoftClassPtr<UQuestReward> QuestReward;
	/**
	* Whether this step successfully completes its associated quest. Multiple steps may end a given quest.
	*
	* If no quest step explicitly ends the quest, then by default, completing the last step in the array of QuestSteps
	* will end the quest. Automatic quest completion can be disabled by unticking bQuestAutoComplete in the
	* Completion > Advanced category.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StepEnd)
	bool bCompletesQuest = false;

	const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }

};

/**
 * Used to flag the required outcome of a given prerequisite quest that must be fulfilled before starting (but not activating)
 * another quest 
 */
UENUM(BlueprintType)
enum class EQuestPrerequisiteOutcome : uint8
{
	AnyOutcome UMETA(DisplayName = "Any Outcome"),
	MustSucceed UMETA(DisplayName = "Must Succeed"),
	MustFail UMETA(DisplayName = "Must Fail"),
};

/**
 * A prerequisite quest for this quest and the required completion status that must be attained before starting (but not activating)
 * this quest. Contains both a quest class and a required outcome.
 */
USTRUCT(BlueprintType)
struct FQuestPrerequisite
{
	GENERATED_BODY()

	/**
	 * The quest class that must be completed before this quest can begin.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TSoftClassPtr<UQuest> QuestClass;

	/**
	 * The required completion status (success, failure, any outcome). 
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	EQuestPrerequisiteOutcome RequiredOutcome = EQuestPrerequisiteOutcome::AnyOutcome;
};

/**
 * A quest is a set of linked goals - or FQuestSteps - that together create a single in-game mission. To create a
 * new quest, create a new child blueprint that inherits this base class. Quests can be triggered by calling the
 * StartQuest function on the UQuestManagerSubsystem class and passing the appropriate Quest class.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuest : public UQuestNode
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

public:
	UQuest();
	void StartQuestStep(int32 InQuestStepID);
	void CheckQuestTarget(UObject* InQuestTargetActor);
	
	DECLARE_DYNAMIC_DELEGATE_OneParam(FOnUpdateQuestText, const FQuestText&, InQuestText);
	DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnSetQuestTextVisibility, bool, bIsVisible, bool, bUseCounter);
	DECLARE_DYNAMIC_DELEGATE_OneParam(FOnQueueCommsEvent, const FCommsEvent&, InCommsEvent);
	DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnQuestStepStarted, UQuest*, ActiveQuest, int32, CompletedStepID);
	DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnQuestStepPrereqsFail, UQuest*, ActiveQuest, int32, StepIDWithActivePrereqs);
	DECLARE_DYNAMIC_DELEGATE_FiveParams(FOnQuestStepComplete, UQuest*, ActiveQuest, int32, CompletedStepID, bool, bDidSucceed, bool, bEndedQuest, UQuestReward*, QuestReward);
	DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnQuestComplete, UQuest*, CompletedQuest, bool, bDidSucceed);
	DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnQuestlineComplete, UQuest*, CompletedQuest, bool, bDidSucceed);
	DECLARE_DELEGATE_FourParams(FOnObjectiveEnabled, UQuest*, UObject*, int32, bool);
	
	FOnUpdateQuestText OnUpdateQuestText;
	FOnSetQuestTextVisibility OnSetQuestTextVisibility;
	FOnQueueCommsEvent OnQueueCommsEvent;
	FOnQuestStepStarted OnQuestStepStarted;
	FOnQuestStepPrereqsFail OnQuestStepPrereqsFail;
	FOnQuestStepComplete OnQuestStepComplete;
	FOnQuestComplete OnQuestComplete;
	FOnQuestlineComplete OnQuestlineComplete;
	FOnObjectiveEnabled OnObjectiveEnabled;
	

protected:
	virtual void PostInitProperties() override; 
	
	/**
	 * All of these quests must be completed prior to the start of this quest. If any quest in this set is not
	 * completed when calling StartQuest, the quest will not be started.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadWrite)
	TArray<FQuestPrerequisite> PrerequisiteQuests = TArray<FQuestPrerequisite>();

	/**
	 * An array of structs containing the data required to create a quest step. A quest step represents a discrete goal
	 * that must be completed to advance the quest until either completion or failure. It also contains all the
	 * information required to populate QuestWidget and CommsWidget on the PlayerOverlay HUD element, but this should
	 * probably be moved to another module.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)	
	TArray<FQuestStep> QuestSteps;
	
	/**
	 * By default, successful completion of the last step in the QuestSteps array will also complete the associated
	 * quest. Disable this property if you do not want this behavior for this quest and instead want to explicitly
	 * declare which step(s) will end the quest by enabling bCompletesQuest on at least one quest step.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = Completion, AdvancedDisplay)
	bool bQuestAutoComplete = true;
	
	 /**
	 * Whether this is the final quest in a questline. A questline may be considered a major plot section or act.
	 * It may be accompanied by significant progression and/or rewards and may immediately precede a level change.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = Completion)
	bool bCompletesQuestline = false;

	/**
	 * The next quests started on successful completion of this quest, if any.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadWrite, Category = Completion)
	TSet<TSoftClassPtr<UQuest>> NextQuestsOnSuccess;

	/**
	 * The next quests started following failure of this quest, if any.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadWrite, Category = Completion)
	TSet<TSoftClassPtr<UQuest>> NextQuestsOnFailure;
	
	/**
	 * Whether this quest has been completed. A quest's conditions may be fulfilled early. Setting this value will allow
	 * the quest to be automatically skipped in any associated questline.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Completion)
	bool bIsCompleted = false;

	/**
	 * Completing this quest will also reset the completion status of quests in this set. They will no longer satisfy
	 * prerequisites. This can be useful to create more elaborate branching or looping behaviors in long quest lines.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = Completion, AdvancedDisplay)
	TSet<TSoftClassPtr<UQuest>> PrerequisitesToReset;

	/**
	 * Quick reference set of active StepIDs
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite)
	TSet<int32> CurrentActiveSteps = TSet<int32>();

	/**
	 * Map of completed StepIDs and a bool that tracks whether they ended in success
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite)
	TMap<int32, bool> CompletedSteps;
	
	/**
	 * Map to UQuestObjective children that contain instructions for each active quest step
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite)
	TMap<int32, TObjectPtr<UQuestObjective>> LoadedQuestObjectives;

	/**
	 * Target classes (i.e., kill, pickup, loot, or other generic targets) for active quest steps.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite)
	TMap<int32, TSubclassOf<AActor>> TargetClassStepMap;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void StepFinished(int32 CompletedStepID, bool bDidSucceed);
	void LoadAutoActivatedQuestSteps();
	bool IsQuestStepValid(int32 InStepID) const;
	void ActivateQuestObjective(int32 InStepID);
	UQuestObjective* LoadQuestObjective(int32 InStepID);
	UQuestObjective* FindLoadedQuestObjective(int32 InStepID);
	
	UFUNCTION()
	void UpdateQuestCounter(int32 InStepID, int32 NewDisplayAmount);
	void FinishQuest(bool bDidSucceed);

	UFUNCTION()
	void OnObjectiveEnabledEvent(UObject* InTargetObject, int32 InStepID, bool bNewIsEnabled);
/*
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
*/
	
public:
	// Legacy shim — NodeGuid is the canonical identifier on UQuestNodeBase
	FORCEINLINE FGuid GetQuestGuid() const { return GetNodeGuid(); }
	const TArray<FQuestPrerequisite>& GetPrerequisiteQuests() const { return PrerequisiteQuests; }
	const TSet<TSoftClassPtr<UQuest>>& GetNextQuestsOnSuccess() const { return NextQuestsOnSuccess; }
	const TSet<TSoftClassPtr<UQuest>>& GetNextQuestsOnFailure() const { return NextQuestsOnFailure; }
	const TSet<TSoftClassPtr<UQuest>>& GetPrerequisitesToReset() const { return PrerequisitesToReset; }
	const TSet<int32>& GetCurrentActiveSteps() const { return CurrentActiveSteps; }
	bool DoesCompleteQuestline() const { return bCompletesQuestline; }
};