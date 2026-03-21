// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestWatcherInterface.h"
#include "QuestWatcherComponent.generated.h"


struct FQuestEndedEvent;
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
	bool bDoWatchQuestEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bDoWatchQuestStepStart = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bDoWatchQuestStepEnd = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bDoWatchQuestEnd = true;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestWatcherComponent : public UQuestComponentBase, public IQuestWatcherInterface
{
	GENERATED_BODY()

public:	
	UQuestWatcherComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestActivated, const FName&, WatchedQuestID);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestStepStarted, const FName&, WatchedQuestID, int32, StartedStepID);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestStepCompleted, const FName&, WatchedQuestID, int32, CompletedStepID, bool, bDidSucceed);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestCompleted, const FName&, WatchedQuestID, bool, bDidSucceed);

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestActivated OnQuestActivated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStepStarted OnQuestStepStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStepCompleted OnQuestStepCompleted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestCompleted OnQuestCompleted;
	
protected:
	virtual void BeginPlay() override;

//	virtual void WatchedQuestActivatedEvent_Implementation(UQuest* InWatchedQuest) override;
	virtual void WatchedQuestActivatedEvent(const FQuestEnabledEvent& QuestEnabledEvent);
//	virtual void WatchedQuestStepStartedEvent_Implementation(UQuest* InWatchedQuest, int32 InStartedStepID) override;
	virtual void WatchedQuestStepStartedEvent(const FQuestStepStartedEvent& QuestStepStartedEvent);
//	virtual void WatchedStepCompletedEvent_Implementation(UQuest* InWatchedQuest, int32 InCompletedStepID, bool bDidSucceed) override;
	virtual void WatchedQuestStepCompletedEvent(const FQuestStepCompletedEvent& QuestStepCompletedEvent);
//	virtual void WatchedQuestCompletedEvent_Implementation(UQuest* InWatchedQuest, bool bDidSucceed) override;
	virtual void WatchedQuestCompletedEvent(const FQuestEndedEvent& QuestEndedEvent);

	void RegisterQuestWatcher();	
private:
	/*
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	bool bDoWatchQuestEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	bool bDoWatchQuestStepStart = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	bool bDoWatchQuestStepEnd = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	bool bDoWatchQuestEnd = true;
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	TMap<TSoftClassPtr<UQuest>, FWatchedQuestSettings> WatchedQuests;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	TMap<TSubclassOf<UQuest>, FQuestActiveStepIDs> ActivatedQuestsMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	TSet<TSoftClassPtr<UQuest>> CompletedQuestClasses;
};
