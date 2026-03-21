// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestGiverInterface.h"
#include "QuestGiverComponent.generated.h"


struct FQuestEnabledEvent;
class UQuestEventBase;
class UQuest;
class UQuestManagerSubsystem;


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestGiverComponent : public UQuestComponentBase, public IQuestGiverInterface
{
	GENERATED_BODY()

public:	
	UQuestGiverComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestGiverActivated, bool, bWasQuestActived, UQuest*, ActivatedQuest, bool, bIsAnyQuestEnabled);
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FOnQuestGiverActivated OnQuestGiverActivated;
	
protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSoftClassPtr<UQuest>> QuestClassesToGive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<TSubclassOf<UQuest>, TObjectPtr<UQuest>> EnabledQuests;
	
private:
	
	UFUNCTION(BlueprintCallable)
	void StartQuest(UQuest* QuestToStart);

	UFUNCTION(BlueprintCallable)
	virtual void SetQuestGiverActivated(const TSubclassOf<UQuest>& QuestClassToEnable, const FName& QuestID, bool bIsQuestActive) override;
	
	void RegisterQuestGiver();
	void RegisterForQuestClass(UClass* LoadedQuestClass);
	void OnQuestEnabledEventReceived(const FQuestEnabledEvent& QuestEnabledEvent);

public:
	UFUNCTION(BlueprintCallable)
	bool CanGiveAnyQuests() const;
	UFUNCTION(BlueprintCallable)
	bool IsQuestEnabled(const FString& QuestID);
};
