// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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

	virtual void OnTargetActivated(FGameplayTag Channel, const FQuestStepStartedEvent& StepStartedEvent);
	virtual void OnTargetDeactivated(FGameplayTag Channel, const FQuestStepCompletedEvent& StepCompletedEvent);
	
	// Step tags this target listens to. Mirrors the giver pattern — configure in the component rather than using actor references.
	// The subsystem publishes step events on the step tag; any target configured with that tag activates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	FGameplayTagContainer StepTagsToWatch;
	
private:
	TMap<FGameplayTag, FDelegateHandle> StepStartedHandles;
	FDelegateHandle StepCompletedHandle;
	FGameplayTag ActiveStepTag;
};
