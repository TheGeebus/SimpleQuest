// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestWatcherInterface.h"
#include "QuestWatcherComponent.generated.h"


struct FQuestEndedEvent;
struct FQuestStartedEvent;
struct FQuestEnabledEvent;
struct FQuestDeactivatedEvent;


USTRUCT(BlueprintType)
struct FWatchedQuestEventSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchActivation = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchDeactivation = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchStart = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchEnd = true;

	/**
	 * If non-empty, OnQuestCompleted only fires when the completion outcome matches one of these tags. If empty, fires for
	 * any outcome (default). Only relevant when bWatchQuestEnd is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories = "Quest.Outcome", EditCondition = "bWatchEnd"))
	FGameplayTagContainer OutcomeFilter;
};




UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestWatcherComponent : public UQuestComponentBase, public IQuestWatcherInterface
{
	GENERATED_BODY()

public:	
	UQuestWatcherComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestActivated, FGameplayTag, QuestTag);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestStarted, FGameplayTag, QuestTag);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestCompleted, FGameplayTag, QuestTag, FGameplayTag, OutcomeTag);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestDeactivated, FGameplayTag, QuestTag);

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestActivated OnQuestActivated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStarted OnQuestStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestCompleted OnQuestCompleted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestDeactivated OnQuestDeactivated;
	
protected:
	virtual void BeginPlay() override;

	virtual void WatchedQuestActivatedEvent(FGameplayTag Channel, const FQuestEnabledEvent& QuestEnabledEvent);
	virtual void WatchedQuestStartedEvent(FGameplayTag Channel, const FQuestStartedEvent& QuestStartedEvent);
	virtual void WatchedQuestCompletedEvent(FGameplayTag Channel, const FQuestEndedEvent& QuestEndedEvent);
	virtual void WatchedQuestDeactivatedEvent(FGameplayTag Channel, const FQuestDeactivatedEvent& QuestDeactivatedEvent);

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;
	
	UFUNCTION(BlueprintCallable)
	void RegisterQuestWatcher();	
private:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	TMap<FGameplayTag, FWatchedQuestEventSettings> WatchedTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	FGameplayTagContainer WatchedStepTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	FGameplayTagContainer ActiveQuestTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Quest", meta=(AllowPrivateAccess=true))
	FGameplayTagContainer CompletedQuestTags;
};
