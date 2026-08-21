// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "Quests/Types/QuestEventPayload.h"
#include "Quests/Types/QuestEventTypes.h"
#include "Quests/Types/QuestObservedTagSpec.h"
#include "Signals/Types/SignalRoutingFlags.h"
#include "QuestObserverComponent.generated.h"


struct FQuestActivatedEvent;
struct FQuestActivationFailedEvent;
struct FQuestEnabledEvent;
struct FQuestDisabledEvent;
struct FQuestGiveBlockedEvent;
struct FQuestStartedEvent;
struct FQuestProgressEvent;
struct FQuestEndedEvent;
struct FQuestDeactivatedEvent;
struct FQuestBlockedEvent;
struct FQuestUnblockedEvent;
struct FQuestProgressRefusedEvent;

class UWorldStateSubsystem;
class UQuestStateSubsystem;


/**
 * Per-observed-quest flags controlling which lifecycle events this observer subscribes to. Mirrors the
 * ObserveQuestLifecycle K2 node's per-event exposure mask but flagged per-quest-tag for finer authoring control.
 * Each flag gates its corresponding subscription in RegisterQuestObserver; unticked flags incur zero
 * subscription cost and skip catch-up for that event.
 *
 * Default values preserve the observer's historical subscription set: Enabled / Started / Completed
 * default-on; everything else opts in explicitly.
 */
USTRUCT(BlueprintType)
struct FObservedQuestEventSettings
{
	GENERATED_BODY()

	/** Quest is offerable — execution reached a giver-gated quest. PrereqStatus carries the current prereq snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveActivated = false;

	/**
	 * An activation attempt against this quest was refused. Carries the EQuestActivationBlocker reason
	 * (UnknownQuest, AlreadyLive, AlreadyPendingGiver, or Blocked). Debug-leaning by default; useful for
	 * lock-feedback / refusal-fanfare gameplay. No catch-up — transient like GiveBlocked / Progress.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveActivationFailed = false;
	
	/** Quest is offerable AND prereqs are satisfied (accept-ready). Most common opt-in for "show giver UI". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveEnabled = true;

	/** Quest was Enabled but a leaf change caused prereqs to no-longer-satisfy. Symmetric to Enabled; rare. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveDisabled = false;

	/** A give attempt was refused. Carries the structured blocker array + the giver actor that initiated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveGiveBlocked = false;

	/** Quest entered Live state. Fires per Activate-input pulse, not just on first transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveStarted = true;

	/** Per-step progress tick during Live phase. Transient; no catch-up. Opt-in (can be noisy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveProgress = false;
	
	/**
	 * A progress attempt against this quest was refused — typically a trigger fire on a step whose Progress
	 * gate isn't open (prereq unsatisfied, Blocked state, etc.). Carries the FQuestActivationBlocker array
	 * plus the originating TriggerContext. Transient; no catch-up — refusals are interaction events, not
	 * recoverable state.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveProgressRefused = false;
	
	/** Quest resolved with an outcome. OutcomeFilter (below) further narrows broadcast to specific outcomes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveCompleted = true;

	/** Quest was deactivated (lifecycle interrupted: external request, cascade, block-with-deactivation, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveDeactivated = false;

	/** Quest's Blocked state fact transitioned from absent to present (SetBlocked utility node or BP-driven
	    SetQuestBlocked). Idempotent: already-blocked re-applications don't fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveBlocked = false;

	/** Quest's Blocked state fact transitioned from present to absent (ClearBlocked utility node or BP-driven
	    ClearQuestBlocked). Symmetric partner to bObserveBlocked. Transient; no catch-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveUnblocked = false;

	/**
	 * If non-empty, OnQuestCompleted only fires when the completion outcome matches one of these tags. If empty,
	 * fires for any outcome (default). Only relevant when bObserveCompleted is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories = "SimpleQuest.Outcome", EditCondition = "bObserveCompleted"))
	FGameplayTagContainer OutcomeFilter;
	
	/**
	 * Routing scope for subscriptions made from this observed-tag entry. Default ExactMatch | Descendants
	 * matches the bus's hierarchical-delivery behavior — events on this tag OR any descendant fire the
	 * observer. Set to ExactMatch alone when descendant events would be noise (e.g., observing a Quest
	 * tag where inner-Step events shouldn't trigger this binding).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESignalRoutingMode Routing = FSignalRoutingDefaults::HierarchicalSubscribe;
};


/**
 * Per-actor lifecycle observer for a curated set of quest tags. Drop on any actor that needs to react to
 * specific quest state changes — UI receptionists, level-bound gameplay objects, world services. Each watched
 * tag's FObservedQuestEventSettings controls which of the 10 lifecycle events broadcast.
 *
 * Surface mirrors the ObserveQuestLifecycle K2 node's per-event delegates: same events, same payload shapes,
 * same catch-up semantics. The observer is the curated component-form alternative for designers who want
 * config-authored per-quest observation rather than ad-hoc K2 subscription.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestObserverComponent : public UQuestComponentBase
{
	GENERATED_BODY()

public:
	UQuestObserverComponent();

	// ─── Observer Events ─────────────────────────────────────────────────────────────────────────
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
	// observer's bound tag (longest descendant where the bound tag is a prefix). Guaranteed to be either
	// the bound tag or a descendant of the bound tag.
	//	- It answers: what's the address of this event in the context I cared about?
	//
	// In single-channel publishes the two are equal; in multi-channel publishes (e.g., a Step inlined
	// under multiple LinkedQuestline contexts) they diverge — QuestTag stays canonical across all
	// observers, MatchedChannel reflects each observer's own perspective. Branch on QuestTag for "what quest
	// instance sent me this"; branch on MatchedChannel for "how was this relevant to my subscription"
	// Mirrors UQuestLifecycleObserver's K2-node delegate contract; same shape, same semantics.
	//
	// ── Offer phase ──────────────────────────────────────────────────────────────────────────────
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestActivated, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Payload, FQuestPrereqStatus, PrereqStatus);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams (FOnQuestActivationFailed, FGameplayTag, QuestTag, FName, AttemptedTagName, FGameplayTag, MatchedChannel, EQuestActivationBlocker, Reason, FQuestEventPayload, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestEnabled, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestDisabled, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestGiveBlocked, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, const TArray<FQuestActivationBlocker>&, Blockers, AActor*, GiverActor);

	// ── Run phase ────────────────────────────────────────────────────────────────────────────────
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestStarted, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Payload, AActor*, GiverActor);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestProgress, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Payload);

	// ── End phase ────────────────────────────────────────────────────────────────────────────────
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestCompleted, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FGameplayTag, OutcomeTag, FQuestEventPayload, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestDeactivated, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestBlocked, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestUnblocked, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, FQuestEventPayload, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams (FOnQuestProgressRefused, FGameplayTag, QuestTag, FGameplayTag, MatchedChannel, const TArray<FQuestActivationBlocker>&, Blockers, const FQuestObjectiveTriggerContext&, TriggerContext);

	// ── Catch-all (any event) ────────────────────────────────────────────────────────────────────
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAnyQuestEvent, FQuestLifecycleEventReport, Report);
	
	/** Fires when execution reaches a giver-gated quest. PrereqStatus describes whether prereqs are currently satisfied. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestActivated OnQuestActivated;
	
	/**
	 * Fires when an activation attempt was refused. Reason discriminates UnknownQuest / AlreadyLive /
	 * AlreadyPendingGiver / Blocked. Debug-leaning by default; useful for lock-feedback gameplay too.
	 */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestActivationFailed OnQuestActivationFailed;

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
	
	/**
	 * Fires when a progress attempt against this quest is refused — typically a trigger fire on a step whose
	 * Progress gate isn't open (prereq unsatisfied, Blocked state, etc.). Mirrors OnQuestGiveBlocked's shape
	 * but for the run phase. Carries the blocker array + originating TriggerContext.
	 */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnQuestProgressRefused OnQuestProgressRefused;
	
	/**
	 * Catch-all delegate that fires for every lifecycle event this observer receives, packaged into a single
	 * FQuestLifecycleEventReport payload. Convenient for broad-audience consumers (UI sidebars, audio routers,
	 * telemetry pipelines) that would otherwise bind every per-type delegate just to route on EventType.
	 *
	 * Gated by the same per-tag opt-in flags in FObservedQuestEventSettings — events with their per-tag flag
	 * off don't fire the catch-all (they never enter the observer's pipeline). Set the bObserveX flags
	 * accordingly when configuring ObservedTags.
	 *
	 * Bind this OR the narrow delegates, not both unless you want the same event delivered twice — there is no
	 * framework-side cross-subscription deduplication. ActivationFailed stays on its narrow delegate only (outside
	 * the lifecycle enum's scope; debug-leaning audience).
	 */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnAnyQuestEvent OnAnyQuestEvent;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	/**
	 * Registration and catch-up, deferred to the tick after BeginPlay so the owning actor finishes its own
	 * initialization (its Event BeginPlay — created state, bound delegates) before any event reaches this
	 * component. Component BeginPlay runs ahead of the actor's, so firing here directly hits a half-built owner.
	 * Virtual so Trigger / Giver chain their role-specific registration via Super.
	 */
	virtual void PerformDeferredRegistration();

	/**
	 * Derived components may expose a set of tags to be implicitly observed alongside the
	 * designer-authored ObservedTags container. Returned tags receive default FObservedQuestEventSettings
	 * (every observe-flag on, no outcome filter) only when not already present in ObservedTags -
	 * designer-authored settings always win on overlap.
	 *
	 * Use to bridge tags managed by a derived component (e.g., Giver's QuestTagsToGive) onto the
	 * Observer's broadcast surface without requiring designers to maintain a parallel ObservedTags entry.
	 */
	virtual TArray<FQuestObservedTagSpec> GetImplicitlyObservedTags() const { return {}; }

	virtual void HandleQuestActivated			(FGameplayTag Channel, const FQuestActivatedEvent& Event);
	virtual void HandleQuestActivationFailed	(FGameplayTag Channel, const FQuestActivationFailedEvent& Event);
	virtual void HandleQuestEnabled				(FGameplayTag Channel, const FQuestEnabledEvent& Event);
	virtual void HandleQuestDisabled			(FGameplayTag Channel, const FQuestDisabledEvent& Event);
	virtual void HandleQuestGiveBlocked			(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event);
	virtual void HandleQuestStarted				(FGameplayTag Channel, const FQuestStartedEvent& Event);
	virtual void HandleQuestProgress			(FGameplayTag Channel, const FQuestProgressEvent& Event);
	virtual void HandleQuestCompleted			(FGameplayTag Channel, const FQuestEndedEvent& Event);
	virtual void HandleQuestDeactivated			(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	virtual void HandleQuestBlocked				(FGameplayTag Channel, const FQuestBlockedEvent& Event);
	virtual void HandleQuestUnblocked			(FGameplayTag Channel, const FQuestUnblockedEvent& Event);
	virtual void HandleQuestProgressRefused		(FGameplayTag Channel, const FQuestProgressRefusedEvent& Event);
	
	/**
	 * Packs the supplied identity and event-specific fields into a FQuestLifecycleEventReport and broadcasts
	 * on OnAnyQuestEvent. Called from every per-event Handle* site (and from each catch-up branch) after
	 * the corresponding narrow delegate broadcasts. No-op if OnAnyQuestEvent has no listeners.
	 */
	void BroadcastAnyQuestEvent(FGameplayTag QuestTag, FGameplayTag MatchedChannel, EQuestLifecycleEventType EventType, const FQuestEventPayload& Payload, FGameplayTag OutcomeTag = FGameplayTag(), AActor* GiverActor = nullptr);

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;
	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove) override;

	UFUNCTION(BlueprintCallable)
	void RegisterQuestObserver();

	/**
	 * Subscribe + source-register + catch up ONE tag. Shared by RegisterQuestObserver's loop and the runtime
	 * AddObservedTag path; captures the bus handles into SubscriptionHandlesByTag.
	 */
	void RegisterSingleObservedTag(const FGameplayTag& QuestTag, const FObservedQuestEventSettings& Settings);

	/** Unsubscribe (by stored handle) + source-unregister + clear bookkeeping for ONE tag. */
	void UnregisterSingleObservedTag(const FGameplayTag& QuestTag);

	/**
	 * Per-tag catch-up: replays current state for QuestTag as synthetic events. Virtual so Trigger/Giver layer
	 * their role catch-up (OnQuestTriggerActivated replay, giver availability) on top via Super.
	 */
	virtual void CatchUpSingleTag(const FGameplayTag& QuestTag, const FObservedQuestEventSettings& Settings, UWorldStateSubsystem* WorldState, UQuestStateSubsystem* QuestState);

	/**
	 * Per-watched-tag bus subscription handles, captured at subscribe time so one tag can be unsubscribed
	 * selectively (RemoveObservedTag / RemoveTagsFromTrigger) without tearing down the whole component.
	 */
	TMap<FGameplayTag, TArray<FDelegateHandle>> SubscriptionHandlesByTag;
	
	/**
	 * True once RegisterQuestObserver has run. Gates the live-vs-deferred branch in AddObservedTag/RemoveObservedTag:
	 * before registration, mutating the container suffices; after, it does live subscribe/unsubscribe work.
	 */
	bool bRegistered = false;
	
private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(Categories="SimpleQuest.Questline", AllowPrivateAccess=true))
	TMap<FGameplayTag, FObservedQuestEventSettings> ObservedTags;

	/** DEPRECATED — add Tag/Settings pairs to UQuestObserverComponent::ObservedTags TMap instead */
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

	/**
	 * Runtime: start watching a quest tag (or update an already-watched tag's settings). If the component has
	 * already registered, it subscribes + catches up the tag immediately; otherwise the tag is picked up by the
	 * deferred registration. Safe to call repeatedly for the same tag — it refreshes rather than double-subscribes.
	 */
	UFUNCTION(BlueprintCallable, Category="Quest")
	void AddObservedTag(FGameplayTag QuestTag, FObservedQuestEventSettings Settings);

	/**
	 * Runtime: stop watching a quest tag. If registered, unsubscribes by handle and drops its source-registry
	 * entry live. No-op for a tag that isn't being watched.
	 */
	UFUNCTION(BlueprintCallable, Category="Quest")
	void RemoveObservedTag(FGameplayTag QuestTag);

	/** DEPRECATED — WatchedStepTags is deprecated and only preserved for backwards compatibility. Prefer UQuestObserverComponent::ObservedTags TMap, using GetObservedTags() */
	const FGameplayTagContainer& GetWatchedStepTags() const { return WatchedStepTags; }
	const TMap<FGameplayTag, FObservedQuestEventSettings>& GetObservedTags() const { return ObservedTags; }
};