// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "QuestManagerSubsystem.generated.h"

class UQuestStep;
struct FGameplayTag;
struct FQuestStartedEvent;
struct FQuestEnabledEvent;
struct FQuestObjectiveTriggered;
class USignalSubsystem;
class UWorldStateSubsystem;
class IQuestTargetInterface;
class UQuestGiverInterface;
struct FQuestText;
class UQuestReward;
class UQuestlineGraph;
class UQuestNodeBase;


/**
 * This class manages quests and quest givers, loading and unloading quests as needed. It handles the registration of actors
 * who have a quest giver component when they begin play. 
 */
UCLASS(Abstract, Blueprintable, config = Game)
class SIMPLEQUEST_API UQuestManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:


	/**
	 * Check if this actor fulfills a quest requirement.
	 * @param InQuestElement A pointer to an element that possibly completes a quest condition. This may be an 
	 * enemy killed, an item used, a trigger in the scene, an action taken, etc. 
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void CountQuestElement(UObject* InQuestElement);
	void CheckQuestObjectives(const FQuestObjectiveTriggered& QuestObjectiveEvent);

	/*
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestTextUpdated, const FQuestText&, InQuestText);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestTextVisibilityUpdated, bool, bIsVisible, bool, bUseCounter);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommsEventStart, const FCommsEvent&, InCommsEvent);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommsEventEnd);

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestTextUpdated OnQuestTextUpdated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestTextVisibilityUpdated OnQuestTextVisibilityUpdated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnCommsEventStart OnCommsEventStart;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnCommsEventEnd OnCommsEventEnd;
	*/

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void StartInitialQuests();


	// TEMPORARY BRIDGE
	UFUNCTION(BlueprintCallable)
	void GiveNodeQuest(FGameplayTag NodeTag);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Quest State")
	bool IsQuestActive(FGameplayTag QuestTag) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Quest State")
	bool IsQuestCompleted(FGameplayTag QuestTag) const;   // either outcome

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Quest State")
	bool IsQuestSucceeded(FGameplayTag QuestTag) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Quest State")
	bool IsQuestFailed(FGameplayTag QuestTag) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Quest State")
	bool IsQuestPendingGiver(FGameplayTag QuestTag) const;

protected:

	/** Questline graph assets to activate when the game launches. Prefer this over InitialQuests for graphs compiled with the current compiler. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="QuestMap")
	TArray<TSoftObjectPtr<UQuestlineGraph>> InitialQuestlines;

	/** Node instances from all loaded questline graph assets, keyed by tag. Populated by ActivateQuestlineGraph. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UQuestNodeBase>> LoadedNodeInstances;
	
	/** Registers all compiled node instances from the graph into LoadedNodeInstances and activates its entry nodes. */
	virtual void ActivateQuestlineGraph(UQuestlineGraph* Graph);

	/** Looks up the instance for NodeTag in LoadedNodeInstances and activates it. */
	virtual void ActivateNodeByTag(FName NodeTag);

	/** Chains to next nodes after a node completes, using tag-based routing from NextNodesOnSuccess / NextNodesOnFailure. */
	virtual void ChainToNextNodes(UQuestNodeBase* CompletedNode, bool bDidSucceed);

	
	/**
	 * HUD stuff - should probably be abstracted into another system that binds to the events on this one 
	 */
	/*
	TArray<FCommsEvent> CommsEventQueue;
	FTimerHandle CommsEventTimerHandle;
	void CommsEventTimerEnd();
	UFUNCTION()
	void QueueCommsEvent(const FCommsEvent& InCommsEvent);
	void StartCommsEvent();
	UFUNCTION()
	void UpdateQuestText(const FQuestText& InQuestText);
	UFUNCTION()
	void UpdateQuestTextVisibility(bool bIsVisible, bool bUseCounter);
	*/

	void PublishQuestEndedEvent(FGameplayTag QuestTag, bool bDidSucceed) const;
	UFUNCTION()
	void OnStepTargetEnabledEvent(UQuestStep* Step, UObject* TargetObject, bool bIsEnabled);

	/*
	// Voice line audio player. 
	UPROPERTY()
	TObjectPtr<UAudioComponent> AudioComponent;

	// How long to keep the subtitles on screen after playing a voice line.
	UPROPERTY(EditDefaultsOnly)
	float CommsEventSubtitleDelay = 3.f;
	*/

	UPROPERTY()
	TObjectPtr<USignalSubsystem> QuestSignalSubsystem;
	
	UPROPERTY()
	TObjectPtr<UWorldStateSubsystem> WorldState;

	FDelegateHandle ObjectiveTriggeredDelegateHandle;

private:
	UFUNCTION()
	void HandleOnNodeCompleted(UQuestNodeBase* Node, bool bDidSucceed);
	UFUNCTION()
	void HandleOnNodeActivated(UQuestNodeBase* Node, FGameplayTag InContextualTag);
	static FGameplayTag MakeQuestStateFact(FGameplayTag QuestTag, const FString& Leaf);
	void SetQuestActive(FGameplayTag QuestTag);
	void SetQuestCompleted(FGameplayTag QuestTag, bool bSucceeded);
	void SetQuestPendingGiver(FGameplayTag QuestTag);
	void ClearQuestPendingGiver(FGameplayTag QuestTag);

	
};
