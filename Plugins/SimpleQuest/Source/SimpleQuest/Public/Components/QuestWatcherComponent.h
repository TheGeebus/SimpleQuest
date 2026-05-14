// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestWatcherInterface.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "Quests/Types/QuestEventPayload.h"
#include "QuestWatcherComponent.generated.h"


struct FQuestActivatedEvent;
struct FQuestEnabledEvent;
struct FQuestDisabledEvent;
struct FQuestGiveBlockedEvent;
struct FQuestStartedEvent;
struct FQuestProgressEvent;
struct FQuestEndedEvent;
struct FQuestDeactivatedEvent;
struct FQuestBlockedEvent;
struct FQuestUnblockedEvent;


/**
 * Per-watched-quest flags controlling which lifecycle events this watcher subscribes to. Mirrors the
 * BindToQuestEvent K2 node's per-event exposure mask but flagged per-quest-tag for finer authoring control.
 * Each flag gates its corresponding subscription in RegisterQuestWatcher; unticked flags incur zero
 * subscription cost and skip catch-up for that event.
 *
 * Default values preserve the watcher's historical subscription set: Enabled / Started / Completed
 * default-on; everything else opts in explicitly.
 */
USTRUCT(BlueprintType)
struct FWatchedQuestEventSettings
{
	GENERATED_BODY()

	/** Quest is offerable — execution reached a giver-gated quest. PrereqStatus carries the current prereq snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchActivated = false;

	/** Quest is offerable AND prereqs are satisfied (accept-ready). Most common opt-in for "show giver UI". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchEnabled = true;

	/** Quest was Enabled but a leaf change caused prereqs to no-longer-satisfy. Symmetric to Enabled; rare. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchDisabled = false;

	/** A give attempt was refused. Carries the structured blocker array + the giver actor that initiated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchGiveBlocked = false;

	/** Quest entered Live state. Fires per Activate-input pulse, not just on first transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchStarted = true;

	/** Per-step progress tick during Live phase. Transient; no catch-up. Opt-in (can be noisy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchProgress = false;

	/** Quest resolved with an outcome. OutcomeFilter (below) further narrows broadcast to specific outcomes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchCompleted = true;

	/** Quest was deactivated (lifecycle interrupted: external request, cascade, block-with-deactivation, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchDeactivated = false;

	/** Quest's Blocked state fact transitioned from absent to present (SetBlocked utility node or BP-driven
	    SetQuestBlocked). Idempotent: already-blocked re-applications don't fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchBlocked = false;

	/** Quest's Blocked state fact transitioned from present to absent (ClearBlocked utility node or BP-driven
	    ClearQuestBlocked). Symmetric partner to bWatchBlocked. Transient; no catch-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bWatchUnblocked = false;

	/**
	 * If non-empty, OnQuestCompleted only fires when the completion outcome matches one of these tags. If empty,
	 * fires for any outcome (default). Only relevant when bWatchCompleted is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories = "SimpleQuest.Outcome", EditCondition = "bWatchCompleted"))
	FGameplayTagContainer OutcomeFilter;
};


/**
 * Per-actor lifecycle observer for a curated set of quest tags. Drop on any actor that needs to react to
 * specific quest state changes — UI receptionists, level-bound gameplay objects, world services. Each watched
 * tag's FWatchedQuestEventSettings controls which of the 10 lifecycle events broadcast.
 *
 * Surface mirrors the BindToQuestEvent K2 node's per-event delegates: same events, same payload shapes,
 * same catch-up semantics. The watcher is the curated component-form alternative for designers who want
 * config-authored per-quest observation rather than ad-hoc K2 subscription.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestWatcherComponent : public UQuestComponentBase, public IQuestWatcherInterface
{
	GENERATED_BODY()

public:
	UQuestWatcherComponent();

	// ── Offer phase ──────────────────────────────────────────────────────────────────────────────
	//
	// Linking Questline graphs means that a single node may broadcast events on several tagged channels
	// that each refer to its address in a different graph hierarchy. Subscribers can listen for any
	// ancestor tag in any of those graphs to receive an event broadcast. Both the true event origin and
	// the signal pathway that resulted in event delivery are provided as separate gameplay tags.
	//
	// QuestTag is the canonical event identity (publishing instance's ContextualTag / Stack[0]). It is
	// the address of the event as seen from the perspective of the graph asset instance responsible for
	// originating the event. It may not be a direct descendant of the bound tag.
	//  - It answers: what graph asset and node sent me this event?
	//
	// MatchedChannel is delivery metadata — the channel from this publish set most specific to this
	// watcher's bound tag (longest descendant where the bound tag is a prefix). Guaranteed to be either
	// the bound tag or a descendant of the bound tag.
	//	- It answers: what's the address of this event in the context I cared about?
	//
	// In single-channel publishes the two are equal; in multi-channel publishes (e.g., a Step inlined
	// under multiple LinkedQuestline contexts) they diverge — QuestTag stays canonical across all
	// watchers, MatchedChannel reflects each watcher's own perspective. Branch on QuestTag for "what quest
	// instance sent me this"; branch on MatchedChannel for "how was this relevant to my subscription"
	// Mirrors UQuestEventSubscription's K2-node delegate contract; same shape, same semantics.
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestActivated,    FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Context, FQuestPrereqStatus, PrereqStatus);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestEnabled,      FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Context);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestDisabled,     FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Context);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestGiveBlocked,  FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, const TArray<FQuestActivationBlocker>&, Blockers, AActor*, GiverActor);

	// ── Run phase ────────────────────────────────────────────────────────────────────────────────
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestStarted,      FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Context, AActor*, GiverActor);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestProgress,     FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Context);

	// ── End phase ────────────────────────────────────────────────────────────────────────────────
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestCompleted,    FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FGameplayTag, OutcomeTag, FQuestEventPayload, Context);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestDeactivated,  FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Context);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestBlocked,      FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Context);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestUnblocked,    FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Context);

	/** Fires when execution reaches a giver-gated quest. PrereqStatus describes whether prereqs are currently satisfied. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestActivated OnQuestActivated;

	/** Fires when a giver-gated quest becomes accept-ready (Activated AND prereqs satisfy). */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestEnabled OnQuestEnabled;

	/** Fires when an accept-ready quest becomes no-longer-ready (NOT-prereq edge case; rare). */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestDisabled OnQuestDisabled;

	/** Fires when a give attempt is refused. Carries blocker array + the giver actor that initiated. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestGiveBlocked OnQuestGiveBlocked;

	/** Fires when the quest enters Live state. GiverActor populated when activation came through a giver. Fires per
	    Activate-input pulse — not just on first transition. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestStarted OnQuestStarted;

	/** Fires on per-step progress ticks during the Live phase. Transient; no catch-up. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestProgress OnQuestProgress;

	/** Fires when the quest resolves with an outcome. Per-watched-tag OutcomeFilter narrows broadcast. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestCompleted OnQuestCompleted;

	/** Fires when the quest is deactivated before completing. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestDeactivated OnQuestDeactivated;

	/** Fires when the quest's Blocked state fact transitions from absent to present. Idempotent at the publisher. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestBlocked OnQuestBlocked;

	/** Fires when the quest's Blocked state fact transitions from present to absent. Symmetric partner to OnQuestBlocked. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestUnblocked OnQuestUnblocked;

protected:
	virtual void BeginPlay() override;

	virtual void HandleQuestActivated   (FGameplayTag Channel, const FQuestActivatedEvent& Event);
	virtual void HandleQuestEnabled     (FGameplayTag Channel, const FQuestEnabledEvent& Event);
	virtual void HandleQuestDisabled    (FGameplayTag Channel, const FQuestDisabledEvent& Event);
	virtual void HandleQuestGiveBlocked (FGameplayTag Channel, const FQuestGiveBlockedEvent& Event);
	virtual void HandleQuestStarted     (FGameplayTag Channel, const FQuestStartedEvent& Event);
	virtual void HandleQuestProgress    (FGameplayTag Channel, const FQuestProgressEvent& Event);
	virtual void HandleQuestCompleted   (FGameplayTag Channel, const FQuestEndedEvent& Event);
	virtual void HandleQuestDeactivated (FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	virtual void HandleQuestBlocked     (FGameplayTag Channel, const FQuestBlockedEvent& Event);
	virtual void HandleQuestUnblocked   (FGameplayTag Channel, const FQuestUnblockedEvent& Event);

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;
	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove) override;

	UFUNCTION(BlueprintCallable)
	void RegisterQuestWatcher();

private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(Categories="SimpleQuest.Questline", AllowPrivateAccess=true))
	TMap<FGameplayTag, FWatchedQuestEventSettings> WatchedTags;

	// DEPRECATED — add Tag/Settings pairs to UQuestWatcherComponent::WatchedTags TMap instead
	FGameplayTagContainer WatchedStepTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Quest", meta=(Categories="SimpleQuest.Questline", AllowPrivateAccess=true))
	FGameplayTagContainer ActiveQuestTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Quest", meta=(Categories="SimpleQuest.Questline", AllowPrivateAccess=true))
	FGameplayTagContainer CompletedQuestTags;

public:
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetRegisteredWatchedStepTags() const;

	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetRegisteredWatchedQuestKeys() const;

	const FGameplayTagContainer& GetWatchedStepTags() const { return WatchedStepTags; }
	const TMap<FGameplayTag, FWatchedQuestEventSettings>& GetWatchedTags() const { return WatchedTags; }
};