// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Interfaces/QuestTargetInterface.h"
#include "QuestTargetComponent.generated.h"


struct FQuestStartedEvent;
struct FQuestEndedEvent;
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

	/**
	 * Publish an objective-triggered event on every watched step channel. CustomData, if supplied, flows through to the
	 * objective's FQuestObjectiveContext::CustomData — designer-visible game-specific payload. BP pin is optional via
	 * AutoCreateRefTerm; C++ callers can omit to publish an empty CustomData.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "CustomData"))
	virtual void SendTriggeredEvent(const FInstancedStruct& CustomData = FInstancedStruct());

	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "CustomData"))
	virtual void SendKilledEvent(AActor* KillerActor, const FInstancedStruct& CustomData = FInstancedStruct());

	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "CustomData"))
	virtual void SendInteractedEvent(AActor* InteractingActor, const FInstancedStruct& CustomData = FInstancedStruct());

protected:
	virtual void BeginPlay() override;

	virtual void OnTargetActivated(FGameplayTag Channel, const FQuestStartedEvent& Event);
	virtual void OnTargetDeactivated(FGameplayTag Channel, const FQuestEndedEvent& Event);
	
	// Step tags this target listens to. Mirrors the giver pattern — configure in the component rather than using actor references.
	// The subsystem publishes step events on the step tag; any target configured with that tag activates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest", meta = (Categories = "Quest"))
	FGameplayTagContainer StepTagsToWatch;

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;
	
private:
	TMap<FGameplayTag, FDelegateHandle> StepStartedHandles;
	
	// Per-step activation tracking — preserves the routing guarantee when multiple watched steps are active simultaneously
	TMap<FGameplayTag, FDelegateHandle> ActiveStepEndHandles;

public:
	const FGameplayTagContainer& GetStepTagsToWatch() const { return StepTagsToWatch; }

};
