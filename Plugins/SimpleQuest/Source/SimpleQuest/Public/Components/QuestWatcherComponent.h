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


USTRUCT(BlueprintType)
struct FWatchedQuestEventSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestStart = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchQuestEnd = true;
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

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestActivated OnQuestActivated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStarted OnQuestStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestCompleted OnQuestCompleted;
	
protected:
	virtual void BeginPlay() override;

	virtual void WatchedQuestActivatedEvent(FGameplayTag Channel, const FQuestEnabledEvent& QuestEnabledEvent);
	virtual void WatchedQuestStartedEvent(FGameplayTag Channel, const FQuestStartedEvent& QuestStartedEvent);
	virtual void WatchedQuestCompletedEvent(FGameplayTag Channel, const FQuestEndedEvent& QuestEndedEvent);

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
