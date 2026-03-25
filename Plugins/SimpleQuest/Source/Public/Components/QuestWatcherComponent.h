// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestWatcherInterface.h"
#include "QuestWatcherComponent.generated.h"


struct FQuestStepPrereqCheckFailed;
struct FQuestPrerequisiteCheckFailed;
struct FQuestEndedEvent;
struct FQuestStartedEvent;
struct FQuestStepCompletedEvent;
struct FQuestStepStartedEvent;
struct FQuestEnabledEvent;
class UQuest;

USTRUCT(BlueprintType)
struct FQuestActiveStepIDs
{
	GENERATED_BODY()

	UPROPERTY()
	TSet<int32> ActiveStepIDs;
};

USTRUCT(BlueprintType)
struct FWatchedQuestSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchPrerequisiteFailure = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestStart = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestStepStart = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestStepPrereqsFailure = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestStepEnd = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestEnd = true;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestWatcherComponent : public UQuestComponentBase, public IQuestWatcherInterface
{
	GENERATED_BODY()

public:	
	UQuestWatcherComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestActivated, const FName&, WatchedQuestID);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestPrerequisiteCheckFailed, const FName&, WatchedQuestID);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestStarted, const FName&, WatchedQuestID);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestStepStarted, const FName&, WatchedQuestID, int32, StartedStepID);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestStepPrereqCheckFailed, const FName&, WatchedQuestID, int32, StartedStepID);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnQuestStepCompleted, const FName&, WatchedQuestID, int32, CompletedStepID, bool, bDidSucceed, bool, bEndedQuest);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestCompleted, const FName&, WatchedQuestID, bool, bDidSucceed);

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestActivated OnQuestActivated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestPrerequisiteCheckFailed OnQuestPrerequisiteCheckFailed;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStarted OnQuestStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStepStarted OnQuestStepStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStepPrereqCheckFailed OnQuestStepPrereqCheckFailed;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStepCompleted OnQuestStepCompleted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestCompleted OnQuestCompleted;
	
protected:
	virtual void BeginPlay() override;

	virtual void WatchedQuestActivatedEvent(const FQuestEnabledEvent& QuestEnabledEvent);
	virtual void WatchedQuestPrerequisitesFailedEvent(const FQuestPrerequisiteCheckFailed& QuestPrerequisitesFailedEvent);
	virtual void WatchedQuestStartedEvent(const FQuestStartedEvent& QuestStartedEvent);
	virtual void WatchedQuestStepStartedEvent(const FQuestStepStartedEvent& QuestStepStartedEvent);
	virtual void WatchedQuestStepPrereqsFailedEvent(const FQuestStepPrereqCheckFailed& QuestStepPrereqCheckFailedEvent);
	virtual void WatchedQuestStepCompletedEvent(const FQuestStepCompletedEvent& QuestStepCompletedEvent);
	virtual void WatchedQuestCompletedEvent(const FQuestEndedEvent& QuestEndedEvent);

	UFUNCTION(BlueprintCallable)
	void RegisterQuestWatcher();	
private:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	TMap<TSoftClassPtr<UQuest>, FWatchedQuestSettings> WatchedQuests;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	TMap<TSubclassOf<UQuest>, FQuestActiveStepIDs> ActivatedQuestsMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	TSet<TSoftClassPtr<UQuest>> CompletedQuestClasses;
};
