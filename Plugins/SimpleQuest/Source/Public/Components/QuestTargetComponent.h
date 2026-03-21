// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestTargetInterface.h"
#include "QuestTargetComponent.generated.h"


struct FQuestStepCompletedEvent;
struct FQuestStepStartedEvent;
class UQuestManagerSubsystem;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestTargetComponent : public UQuestComponentBase, public IQuestTargetInterface
{
	GENERATED_BODY()

public:	
	UQuestTargetComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActivateQuestTargetDelegate, bool, bIsActivated);

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FActivateQuestTargetDelegate OnQuestTargetActivated;
			
	UFUNCTION(BlueprintCallable)
	virtual void SetActivated_Implementation(bool bIsActivated) override;

	UFUNCTION(BlueprintCallable)
	virtual void GetTriggered();
	
	UFUNCTION(BlueprintCallable)
	virtual void GetKilled(AActor* KillerActor);

	UFUNCTION(BlueprintCallable)
	virtual void GetInteracted(AActor* InteractingActor);

protected:
	virtual void PostInitProperties() override;
	virtual void BeginPlay() override;

	virtual void OnTargetActivated(const FQuestStepStartedEvent& StepStartedEvent);
	virtual void OnTargetDeactivated(const FQuestStepCompletedEvent& StepCompletedEvent);

private:

	FDelegateHandle OnQuestTargetActivatedDelegateHandle;
	FDelegateHandle OnQuestTargetDeactivatedDelegateHandle;
};
