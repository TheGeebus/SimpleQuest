// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestObserverComponent.h"
#include "Components/ActorComponent.h"
#include "QuestTriggerComponent.generated.h"


struct FQuestDeactivatedEvent;
struct FQuestStartedEvent;
struct FQuestEndedEvent;
struct FQuestObjectiveTriggerContext;
class UQuestManagerSubsystem;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestTriggerComponent : public UQuestObserverComponent
{
	GENERATED_BODY()

public:	
	UQuestTriggerComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActivateQuestTriggerDelegate, bool, bIsActivated);

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FActivateQuestTriggerDelegate OnQuestTriggerActivated;

	/**
	 * Framework calls this on the lifecycle transitions of the trigger's watched steps — true on step Started,
	 * false on step Completed/Deactivated. BlueprintNativeEvent: adopters override SetActivated in BP to drive
	 * owner visuals / collision / AI behavior on top of (or instead of) the OnQuestTriggerActivated delegate
	 * broadcast. Default impl forwards to OnQuestTriggerActivated.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void SetActivated(bool bIsActivated);

	/**
	 * Publish a trigger event on every watched step channel. The structural facts of any trigger fire are
	 * "what was triggered" (TriggeredActor — typically this component's owning actor; component fills this
	 * in if the caller left it null) and "what initiated the trigger" (Instigator — the killer, interactor,
	 * or whatever external causer fired the trigger; equal to TriggeredActor for self-fired triggers).
	 * CustomData on the context carries game-specific payload. All other variants — kill / interact / any
	 * domain-specific firing — collapse into this one signal; the semantic differentiation lives in the
	 * Instigator role and any CustomData the designer adds.
	 *
	 * BP pin is optional via AutoCreateRefTerm; callers can omit Context to publish with TriggeredActor =
	 * this component's owning actor and everything else empty.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "Context"))
	virtual void SendTriggerEvent(const FQuestObjectiveTriggerContext& Context = FQuestObjectiveTriggerContext());

protected:
	virtual void BeginPlay() override;

	virtual void OnTriggerActivated(FGameplayTag Channel, const FQuestStartedEvent& Event);

	/** Step-completion handler. Routes to OnTriggerStepEnded so completion + deactivation share the same
	 *  "step no longer active" cleanup path. */
	virtual void OnTriggerStepCompleted(FGameplayTag Channel, const FQuestEndedEvent& Event);

	/** Step-deactivation handler. Routes to OnTriggerStepEnded for shared cleanup. Subscribed alongside
	 *  the completion handler because targets disable on either kind of end — completion AND mid-flight
	 *  interruption both indicate "this step is no longer active and this target shouldn't respond." */
	virtual void OnTriggerStepDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	
	/**
	 * Step tags this target listens to. Mirrors the giver pattern — configure in the component rather than using actor references.
	 * The subsystem publishes step events on the step tag; any target configured with that tag activates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest", meta = (Categories = "SimpleQuest.Questline"))
	FGameplayTagContainer StepTagsToTrigger;

	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove) override;

	/**
	 * Bridges StepTagsToTrigger onto the inherited Observer broadcast surface — adopters binding the
	 * inherited Observer delegates (OnQuestStarted, OnQuestProgress, OnQuestCompleted, etc.) receive
	 * fires for the Trigger's managed step tags without authoring a parallel ObservedTags entry.
	 * Chains via Super so a derived class that also bridges its own container (Giver's QuestTagsToGive)
	 * sees both contributions in EffectiveObserved at register time.
	 */
	virtual FGameplayTagContainer GetImplicitlyObservedTags() const override;

	/**
	 * Shared cleanup body for both completion and deactivation routes. Unsubscribes the step end handle for
	 * Channel; if no other watched steps remain active, calls SetActivated(false).
	 */
	void OnTriggerStepEnded(FGameplayTag Channel);

private:	
	/** Per-step activation tracking — preserves the routing guarantee when multiple watched steps are active simultaneously */
	TMap<FGameplayTag, FDelegateHandle> ActiveStepEndHandles;
	
	/**
	 * Per-step deactivation subscription handle. Parallel to ActiveStepEndHandles; subscribed at the same time
	 * to FQuestDeactivatedEvent on the same Channel.
	 */
	TMap<FGameplayTag, FDelegateHandle> ActiveStepDeactivatedHandles;

public:	
	/**
	 * Raw authored StepTagsToTrigger. May contain stale tags — feed into tag-library calls via GetRegisteredStepTagsToTrigger()
	 * instead to avoid UE's stale-tag ensure.
	 */
	const FGameplayTagContainer& GetStepTagsToTrigger() const { return StepTagsToTrigger; }

	/**
	 * Registration-filtered view of StepTagsToTrigger — safe to pass into FGameplayTagContainer::Filter / HasAny /
	 * MatchesAny. Stale entries are dropped with a Warning log; authored container is unchanged.
	 */
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetRegisteredStepTagsToTrigger() const;

};

