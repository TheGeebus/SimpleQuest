// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestObserverComponent.h"
#include "Components/ActorComponent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestProgressRefusedEvent.h"
#include "Events/QuestTriggerDeactivatedEvent.h"
#include "Events/QuestTriggerResponseEvent.h"
#include "Events/QuestTriggerSatisfiedEvent.h"
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

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FActivateQuestTriggerDelegate, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestStartedEvent, Event);

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FActivateQuestTriggerDelegate OnQuestTriggerActivated;
	
	/**
	 * Fires when an objective produces a per-fire response to one of this trigger's SendTriggerEvent fires. Resolution
	 * discriminates Progress / Completed / Refused. Subscriber-side filter on Event.TriggerContext.TriggeredActor ==
	 * GetOwner() is applied internally before broadcast — adopters bound to this delegate only receive responses for
	 * their own trigger's fires.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestTriggerResponded, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestTriggerResponseEvent, Event);
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FOnQuestTriggerResponded OnQuestTriggerResponded;

	/**
	 * Fires when SendTriggerEvent reached a watched step that's been activated (PendingGiver-or-similar) but cannot
	 * currently progress because of structural blockers (Blocked fact set, or unmet prereqs). Mirrors the Giver's
	 * OnQuestGiveBlocked shape — same FQuestActivationBlocker[] payload. Own-fire filter applied internally.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestTriggerBlocked, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestProgressRefusedEvent, Event);
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FOnQuestTriggerBlocked OnQuestTriggerBlocked;

	/**
	 * Fires when the trigger-side of a watched step's lifecycle wraps — Completed / Interrupted (manager-published
	 * alongside FQuestEndedEvent / FQuestDeactivatedEvent) or Manual (from inside an objective via
	 * PublishTriggerDeactivation). Per-lifecycle signal — fires once per step end. No own-fire filter; all trigger
	 * actors watching the step are relevant audiences for lifecycle wrap.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestTriggerDeactivated, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestTriggerDeactivatedEvent, Event);
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FOnQuestTriggerDeactivated OnQuestTriggerDeactivated;

	/**
	 * Fires when an Objective signals that this component's owning actor has been consumed by a multi-target
	 * satisfaction list. Own-fire filter (SatisfiedActor == GetOwner()) applied internally. Adopters bind to
	 * disable trigger visuals / collision per-target.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestTriggerSatisfied, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestTriggerSatisfiedEvent, Event);
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FOnQuestTriggerSatisfied OnQuestTriggerSatisfied;

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
	virtual void PerformDeferredRegistration() override;

	virtual void OnTriggerActivated(FGameplayTag Channel, const FQuestStartedEvent& Event);

	/**
	 * Step-completion handler. Routes to OnTriggerStepEnded so completion + deactivation share the same
	 * "step no longer active" cleanup path.
	 */
	virtual void OnTriggerStepCompleted(FGameplayTag Channel, const FQuestEndedEvent& Event);

	/**
	 * Step-deactivation handler. Routes to OnTriggerStepEnded for shared cleanup. Subscribed alongside
	 * the completion handler because targets disable on either kind of end — completion AND mid-flight
	 * interruption both indicate "this step is no longer active and this target shouldn't respond." */
	virtual void OnTriggerStepDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	
	/**
	 * Receives the objective's per-fire Response (Progress / Completed / Refused). Applies own-fire filter
	 * (TriggerContext.TriggeredActor == GetOwner()) then broadcasts OnQuestTriggerResponded.
	 */
	virtual void HandleQuestTriggerResponse(FGameplayTag Channel, const FQuestTriggerResponseEvent& Event);

	/**
	 * Receives Blocked publishes from any Trigger Component's SendTriggerEvent (including this one's own publishes
	 * bouncing back). Own-fire filter scopes the broadcast to this component's owning actor.
	 */
	virtual void HandleQuestTriggerBlocked(FGameplayTag Channel, const FQuestProgressRefusedEvent& Event);

	/**
	 * Receives the trigger-side wrap signal — Completed / Interrupted / Manual. No own-fire filter; all watching
	 * components on the channel are relevant.
	 */
	virtual void HandleQuestTriggerDeactivated(FGameplayTag Channel, const FQuestTriggerDeactivatedEvent& Event);

	/**
	 * Receives the per-actor satisfaction event. Filters by SatisfiedActor == GetOwner() then fires the delegate
	 * and the BlueprintNativeEvent.
	 */
	virtual void HandleQuestTriggerSatisfied(FGameplayTag Channel, const FQuestTriggerSatisfiedEvent& Event);
	
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
	virtual TArray<FQuestObservedTagSpec> GetImplicitlyObservedTags() const override;

	/**
	 * Shared cleanup body for both completion and deactivation routes. Unsubscribes the step end handles and
	 * clears the channel's satisfied-state bookkeeping. Per-step deactivation broadcasts happen in
	 * HandleQuestTriggerDeactivated (bus path with rich FQuestTriggerDeactivatedEvent payload).
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

	/**
	 * Per-step-channel satisfied state. Cleared when the step's lifecycle wraps via OnTriggerStepEnded. Lets one
	 * Trigger Component watching multiple steps track per-step satisfaction independently.
	 */
	TSet<FGameplayTag> SatisfiedStepChannels;

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

