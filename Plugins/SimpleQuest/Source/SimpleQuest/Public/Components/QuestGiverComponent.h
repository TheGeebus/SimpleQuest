// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Events/QuestStartedEvent.h"
#include "Interfaces/QuestGiverInterface.h"
#include "QuestGiverComponent.generated.h"


struct FQuestGiverRegisteredEvent;
struct FQuestDeactivatedEvent;
struct FQuestEnabledEvent;
class UQuestManagerSubsystem;


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestGiverComponent : public UQuestComponentBase, public IQuestGiverInterface
{
	GENERATED_BODY()

public:	
	UQuestGiverComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestGiverActivated, bool, bWasQuestActivated, FGameplayTag, QuestTag, bool, bIsAnyQuestEnabled);
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FOnQuestGiverActivated OnQuestGiverActivated;
	
protected:
	virtual void BeginPlay() override;
	/**
	 * Transitional — internal subscription and registration still uses this.
	 * Will be removed when QuestTagsToGive drives the full component lifecycle.
	 */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite)
	//TArray<TSoftClassPtr<UQuest>> QuestClassesToGive;
	
	/**
	 * New authoritative property — read by the manifest builder and eventually the sole registration mechanism once the
	 * full tag-based rework is complete.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(Categories="Quest"))
	FGameplayTagContainer QuestTagsToGive;
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Quest", meta=(Categories="Quest", AllowPrivateAccess=true))
	FGameplayTagContainer EnabledQuestTags;

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;

private:
	
	//UFUNCTION(BlueprintCallable)
	//void GiveQuest(UQuest* QuestToStart);

	UFUNCTION(BlueprintCallable)
	void GiveQuestByTag(const FGameplayTag& QuestTag);

	UFUNCTION(BlueprintCallable)
	virtual void SetQuestGiverActivated(const FGameplayTag& QuestTag, bool bIsQuestActive) override;
	
	void RegisterQuestGiver();
	void OnQuestEnabledEventReceived(FGameplayTag Channel, const FQuestEnabledEvent& Event);
	void OnQuestStartedEventReceived(FGameplayTag Channel, const FQuestStartedEvent& Event);
	void OnQuestDeactivatedEventReceived(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

public:
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetQuestTagsToGive() const { return QuestTagsToGive; }
	UFUNCTION(BlueprintCallable)
	bool CanGiveAnyQuests() const;
	UFUNCTION(BlueprintCallable)
	bool IsQuestEnabled(FGameplayTag QuestTag);

};
