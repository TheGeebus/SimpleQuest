// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Interfaces/QuestTargetInterface.h"
#include "QuestTargetComponent.generated.h"


struct FQuestDeactivatedEvent;
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
	 * objective's FQuestObjectiveTriggerContext::CustomData — designer-visible game-specific payload. BP pin is optional via
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

	/** Step-completion handler. Routes to OnTargetStepEnded so completion + deactivation share the same
	 *  "step no longer active" cleanup path. */
	virtual void OnTargetStepCompleted(FGameplayTag Channel, const FQuestEndedEvent& Event);

	/** Step-deactivation handler. Routes to OnTargetStepEnded for shared cleanup. Subscribed alongside
	 *  the completion handler because targets disable on either kind of end — completion AND mid-flight
	 *  interruption both indicate "this step is no longer active and this target shouldn't respond." */
	virtual void OnTargetStepDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);

protected:
	
	/**
	 * Step tags this target listens to. Mirrors the giver pattern — configure in the component rather than using actor references.
	 * The subsystem publishes step events on the step tag; any target configured with that tag activates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest", meta = (Categories = "SimpleQuest.Questline"))
	FGameplayTagContainer StepTagsToWatch;

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;

	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove) override;
	
private:

	/**
	 * Shared cleanup body for both completion and deactivation routes. Unsubscribes the step end handle for
	 * Channel; if no other watched steps remain active, calls SetActivated(false).
	 */
	void OnTargetStepEnded(FGameplayTag Channel);
	
	TMap<FGameplayTag, FDelegateHandle> StepStartedHandles;
	
	/** Per-step activation tracking — preserves the routing guarantee when multiple watched steps are active simultaneously */
	TMap<FGameplayTag, FDelegateHandle> ActiveStepEndHandles;
	
	/**
	 * Per-step deactivation subscription handle. Parallel to ActiveStepEndHandles; subscribed at the same time
	 * to FQuestDeactivatedEvent on the same Channel.
	 */
	TMap<FGameplayTag, FDelegateHandle> ActiveStepDeactivatedHandles;
	

public:
	/**
	 * Raw authored StepTagsToWatch. May contain stale tags — feed into tag-library calls via GetRegisteredStepTagsToWatch()
	 * instead to avoid UE's stale-tag ensure.
	 */
	const FGameplayTagContainer& GetStepTagsToWatch() const { return StepTagsToWatch; }

	/**
	 * Registration-filtered view of StepTagsToWatch — safe to pass into FGameplayTagContainer::Filter / HasAny /
	 * MatchesAny. Stale entries are dropped with a Warning log; authored container is unchanged.
	 */
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetRegisteredStepTagsToWatch() const;

};

