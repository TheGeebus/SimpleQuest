// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "QuestTypes.h"
#include "QuestManagerSubsystem.generated.h"

class UQuestReward;
struct FQuestStartedEvent;
struct FTryQuestStartEvent;
struct FQuestRegistrationEvent;
struct FQuestEnabledEvent;
struct FQuestObjectiveTriggered;
class UQuestSignalSubsystem;
class IQuestTargetInterface;
class UQuestGiverInterface;
struct FQuestText;
class UQuest;

USTRUCT(BlueprintType)
struct FQuestGivers
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TSet<UObject*> QuestGiverSet;
};

USTRUCT(BlueprintType)
struct FQuestWatchers
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TSet<UObject*> QuestWatcherSet;
};

/**
 * This class manages quests and quest givers, loading and unloading quests as needed. It handles the registration of actors
 * who have a quest giver component when they begin play. 
 */
UCLASS(Abstract, Blueprintable)
class SIMPLEQUEST_API UQuestManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:
	/**
	 * Start a quest, loading it first if necessary. 
	 * @param InQuestClass the class to start, a blueprintable child of the UQuest class.
	 * @return True if the quest was successfully started. False if it was not, possibly activating quest givers that
	 * were registered to give this quest instead.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool StartQuest(const TSoftClassPtr<UQuest>& InQuestClass);

	/**
	 * Check if this actor fulfills a quest requirement.
	 * @param InQuestElement A pointer to an element that possibly completes a quest condition. This may be an 
	 * enemy killed, an item used, a trigger in the scene, an action taken, etc. 
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void CountQuestElement(UObject* InQuestElement);
	void CheckQuestObjectives(const FQuestObjectiveTriggered& QuestObjectiveEvent);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetClassAdded, TSubclassOf<AActor>, InTargetClass);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetClassRemoved, TSubclassOf<AActor>, InTargetClass);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestTextUpdated, const FQuestText&, InQuestText);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestTextVisibilityUpdated, bool, bIsVisible, bool, bUseCounter);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommsEventStart, const FCommsEvent&, InCommsEvent);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommsEventEnd);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnQuestEnd);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnQuestlineEnd);
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnTargetClassAdded OnTargetClassAdded;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnTargetClassRemoved OnTargetClassRemoved;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestTextUpdated OnQuestTextUpdated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestTextVisibilityUpdated OnQuestTextVisibilityUpdated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnCommsEventStart OnCommsEventStart;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnCommsEventEnd OnCommsEventEnd;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestEnd OnQuestEnd;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestlineEnd OnQuestlineEnd;

	/**
	 * Register a quest giver for a specific quest, which may not be loaded yet. Maps the two elements using the soft class
	 * reference as the key and a set of all registered quest givers as the value. This allows any number of quest givers
	 * to be notified when a quest can be given once it has been loaded.
	 * 
	 * @param QuestGiverComponent the quest giver component to register, an actor component which enables quest giving
	 * functionality. (See: UQuestGiverComponent::RegisterQuestGiver)
	 * @param InQuestClass the UQuest that this component is attempting to register for. The quest class will be added to
	 * the map if it is not already present. When this quest is activated, all registered quest givers will be notified
	 * through a blueprint native event on the quest giver interface, SetQuestGiverActivated.
	 */
	// void RegisterQuestGiver(UActorComponent* QuestGiverComponent, const TSoftClassPtr<UQuest>& InQuestClass);
	void RegisterQuestGiver(const FQuestRegistrationEvent& Event);

	/**
	 * Register a quest watcher for a specific quest, which may not be loaded yet. Maps the two elements using the soft class
	 * reference as the key and a set of all registered quest givers as the value. Quest watchers will be notified of the
	 * status of completion of every step in the quests they are watching, allowing them to modify their behavior.
	 * 
	 * @param QuestWatcherRegistrationEvent
	 * @param QuestWatcherComponent the quest giver component to register, an actor component which enables quest watching
	 * functionality. (See: UQuestWatcherComponent::RegisterQuestWatcher)
	 * @param InQuestClass the UQuest that this component is attempting to register for. The quest class will be added to
	   * the map if it is not already present. As this quest progresses, registered quest watchers will be notified through
	   * the events on the IQuestWatcherInterface class.
	 */
	void RegisterQuestWatcher(const FQuestRegistrationEvent& QuestWatcherRegistrationEvent);
	void OnTryQuestStartEvent(const FTryQuestStartEvent& Event);
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="RegisteredActorComponents")
	TMap<TSoftClassPtr<UQuest>, FQuestGivers> QuestGiverMap;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="RegisteredActorComponents")
	TMap<TSoftClassPtr<UQuest>, FQuestWatchers> QuestWatcherMap;

	UFUNCTION(BlueprintCallable)
	bool ArePrerequisitesComplete(UQuest* StartedQuest) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void StartInitialQuests();

protected:
	// Quests to load when the game launches. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuestMap", meta = (AllowPrivateAccess = "true"))
	TArray<TSoftClassPtr<UQuest>> InitialQuests;
	
	// Quests currently loaded by the quest manager subsystem.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LoadedQuests", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UQuest>> LoadedQuests;

	// Quick reference set of loaded quests.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LoadedQuests", meta = (AllowPrivateAccess = "true"))
	TSet<TSubclassOf<UQuest>> LoadedQuestClasses;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LoadedQuests", meta = (AllowPrivateAccess = "true"))
	TSet<TSubclassOf<UQuest>> StartedQuestClasses;
	
	// Completed quests. Checked against quest prerequisites to determine eligibility.
	UPROPERTY()
	TMap<TSubclassOf<UQuest>, bool> CompletedQuestClasses;
	
	UQuest* LoadQuest(const TSoftClassPtr<UQuest>& QuestClass);
	UQuest* FindLoadedQuestByClass(const TSubclassOf<UQuest>& InQuest);
	void DeactivateQuestGivers(const UQuest* DeactivatedQuest) const;
	UFUNCTION()
	void OnQuestStepStartedEvent(UQuest* ActiveQuest, int32 StartedQuestStepID);
	UFUNCTION()
	void OnQuestStepEndedEvent(UQuest* ActiveQuest, int32 CompletedQuestStep, bool bDidSucceed, bool bEndedQuest, UQuestReward* Reward);
	void PublishQuestEndEvent(const UQuest* EndedQuest, bool bDidSucceed) const;
	void SetQuestEnabled(FName LoadedQuestID, const TSubclassOf<UQuest>& LoadedQuestClass, bool bIsEnabled);
	void ActivateQuestClass(const TSoftClassPtr<UQuest>& InQuestClass);

	/**
	 * HUD stuff - should probably be abstracted into another system that binds to the events on this one 
	 */
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
	
	UFUNCTION()
	void CompleteQuest(UQuest* CompletedQuest, bool bDidSucceed);
	UFUNCTION()
	void CompleteQuestline(UQuest* CompletedQuest, bool bDidSucceed);
	UFUNCTION()
	void OnQuestTargetEnabledEvent(UQuest* InQuest, UObject* TargetObject, int32 InStepID, bool bNewIsEnabled);

	// Voice line audio player. 
	UPROPERTY()
	TObjectPtr<UAudioComponent> AudioComponent;

	// How long to keep the subtitles on screen after playing a voice line.
	UPROPERTY(EditDefaultsOnly)
	float CommsEventSubtitleDelay = 3.f;

	UPROPERTY()
	TObjectPtr<UQuest> QuestDefaultObject;

	UPROPERTY()
	TObjectPtr<UQuestSignalSubsystem> QuestSignalSubsystem;

	FDelegateHandle QuestWatcherRegistrationDelegateHandle;
	FDelegateHandle QuestGiverRegistrationDelegateHandle;
	FDelegateHandle ObjectiveTriggeredDelegateHandle;
};
