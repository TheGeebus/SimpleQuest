// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestTriggerComponent.h"
#include "Components/ActorComponent.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "QuestGiverComponent.generated.h"


struct FQuestEndedEvent;
struct FQuestGiverRegisteredEvent;
struct FQuestDeactivatedEvent;
struct FQuestEnabledEvent;
struct FQuestActivatedEvent;
struct FQuestDisabledEvent;
struct FQuestGiveBlockedEvent;
struct FQuestStartedEvent;
struct FQuestActivationBlocker;

class UQuestManagerSubsystem;
class UQuestStateSubsystem;


/**
 * Categorization of what caused an availability change on a Giver. Designer-side logic branches
 * on Reason for differentiated UI treatment - e.g., "newly available" pulse vs. "lost availability"
 * fade.
 */
UENUM(BlueprintType)
enum class EGiveAvailabilityChangeReason : uint8
{
	/** Default / unset. Treat as "unspecified change." */
	Unknown,

	/**
	 * A quest reached this giver (entered Activated state). Tag appears in NewlyActivated;
	 * usually also NewlyEnabled if prereqs are already satisfied.
	 */
	Activated,

	/**
	 * A quest's prereqs transitioned from unsatisfied to satisfied while the quest was already
	 * Activated. Tag appears in NewlyEnabled only (not NewlyActivated).
	 */
	Enabled,

	/**
	 * A quest's prereqs transitioned from satisfied to unsatisfied while still Activated.
	 * Tag appears in NewlyUnavailable only (not NewlyDeactivated).
	 */
	Disabled,

	/** A quest was successfully given by this giver. Tag appears in NewlyUnavailable and NewlyDeactivated. */
	Started,

	/**
	 * A quest left this giver's surface without being given (cascade interrupt, force-deactivate,
	 * resolved elsewhere). Tag appears in NewlyUnavailable and NewlyDeactivated.
	 */
	Deactivated,

	/**
	 * Initial state populated at BeginPlay from any quests already pending-giver when this
	 * component came online. Currently* containers reflect the captured snapshot; Newly*
	 * containers may be non-empty if pre-existing quests were caught up.
	 */
	InitialCatchUp,
};


/**
 * Rich payload for OnGiveAvailabilityChanged. Describes the delta between the prior state and
 * the current state, plus the post-change snapshots of both Activated and Enabled sets for
 * convenience - designer doesn't need a follow-up GetActivatedQuests() / GetEnabledQuests()
 * call to refresh UI. Reason discriminates the cause for branched designer logic.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FGiveAvailabilityChange
{
	GENERATED_BODY()

	/** Quests that just entered the enabled set (newly available to give). */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer NewlyEnabled;

	/** Quests that just left the enabled set (newly unavailable to give). */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer NewlyUnavailable;

	/**
	 * Quests that just entered Activated (reached this giver) but aren't yet enabled. Useful for
	 * "this quest exists in scope but can't be given yet" UI states.
	 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer NewlyActivated;

	/** Quests that just left Activated (deactivated, completed elsewhere, etc.). */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer NewlyDeactivated;

	/** Current full enabled set. Convenience snapshot so consumers don't need a follow-up GetEnabledQuests() call. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer CurrentEnabled;

	/** Current full activated set. Convenience snapshot. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer CurrentActivated;

	/** Categorization of the change for branched designer logic. */
	UPROPERTY(BlueprintReadOnly)
	EGiveAvailabilityChangeReason Reason = EGiveAvailabilityChangeReason::Unknown;
};


/**
 * Fires when the giveable set changes. Designer refreshes UI from the rich payload describing
 * which quests entered or left the Activated / Enabled sets.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGiveAvailabilityChanged, FGiveAvailabilityChange, Change);

/**
 * Fires when THIS giver's offer was accepted and the quest went Live. QuestTag is the tag this giver is
 * authored to offer, which stays stable across a multi-tag publish where the canonical identity does not.
 * It does not fire when something else starts the same quest - that is a lifecycle event, not a give.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestGiven, FGameplayTag, QuestTag, FQuestEventPayload, Payload);

/**
 * Fires when THIS giver's offer was refused. Blockers carries one entry per distinct reason. Partner to
 * OnQuestGiven: a give attempt ends in exactly one of the two.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGiveRefused, FGameplayTag, QuestTag, const TArray<FQuestActivationBlocker>&, Blockers);


/**
 * Component for actors that offer quests to the player. Configure QuestTagsToGive with the
 * quest tags this giver should offer; the component automatically tracks Activated / Enabled /
 * Given state for those quests via the lifecycle event bus. Designer refreshes UI from
 * OnGiveAvailabilityChanged plus the state queries (GetEnabledQuests / CanGiveAnyQuests / etc.)
 * and triggers gives via GiveQuest / GiveAllQuests.
 *
 * Per-give success and refusal notifications come from the inherited UQuestObserverComponent
 * delegates OnQuestStarted (Live transition with GiverActor populated) and OnQuestGiveBlocked
 * (refusal with Blockers and GiverActor). QuestTagsToGive entries are implicitly observed -
 * adopters can bind those delegates without authoring a parallel ObservedTags entry. Filter by
 * GiverActor == GetOwner() to scope to this giver's attempts.
 *
 * Inherits the full observation surface from UQuestObserverComponent and the trigger /
 * Send-event surface from UQuestTriggerComponent. A single Giver component can offer quests
 * AND act as a trigger target for other quests' step objectives - populate both
 * QuestTagsToGive and StepTagsToTrigger on the same component.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestGiverComponent : public UQuestTriggerComponent
{
	GENERATED_BODY()

public:	
	UQuestGiverComponent();

	// ── Event surface ────────────────────────────────────────────

	/** Fires when the giveable set changes. Rich payload describes the delta + current snapshot. */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnGiveAvailabilityChanged OnGiveAvailabilityChanged;

	/** This giver's offer was accepted and the quest is now Live. */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnQuestGiven OnQuestGiven;

	/** This giver's offer was refused. */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnGiveRefused OnGiveRefused;


	// ── Action API ───────────────────────────────────────────────

	/**
	 * Give a specific quest. Publishes a request to the manager, which clears any PendingGiver
	 * state, stamps Context onto the target step, and routes into the normal activation pipeline.
	 * The outcome surfaces asynchronously via the inherited OnQuestStarted (success) or
	 * OnQuestGiveBlocked (refusal) delegates; filter by GiverActor == GetOwner() to scope to this
	 * giver's attempts. If Context.Instigator is unset, it defaults to this giver's owning actor.
	 *
	 * @param QuestTag   The quest to give. Must be registered in the runtime tag manager.
	 * @param Context    Per-call activation context. Empty default carries no extra data.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "Context"))
	void GiveQuest(const FGameplayTag& QuestTag, const FQuestObjectiveActivationParams& Context = FQuestObjectiveActivationParams());

	/**
	 * Give every currently-enabled quest in QuestTagsToGive. Iterates in authored order and
	 * calls GiveQuest on each. The same Context applies to all. Useful for "interact with NPC,
	 * give everything available" interaction patterns where designer doesn't want to pick
	 * individually.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "Context"))
	void GiveAllQuests(const FQuestObjectiveActivationParams& Context = FQuestObjectiveActivationParams());

	/**
	 * Runtime: start offering quest tags. If already registered, subscribes + catches up (PendingGiver state) and
	 * fires the availability change. Idempotent per tag.
	 */
	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	void AddTagsToGive(const FGameplayTagContainer& Tags);

	/**
	 * Runtime: stop offering quest tags - fires the availability change for any currently-offered tag, then
	 * unsubscribes and drops the giver source-registry entries.
	 */
	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	void RemoveTagsFromGive(const FGameplayTagContainer& Tags);

	
	// ── State queries ────────────────────────────────────────────

	/**
	 * Activated set: every quest in QuestTagsToGive that has reached this giver, regardless of
	 * whether prereqs are satisfied. Use for "this quest exists in this NPC's scope" UI.
	 */
	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	const FGameplayTagContainer& GetActivatedQuests() const { return ActivatedQuestTags; }

	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	bool HasAnyActivatedQuests() const { return !ActivatedQuestTags.IsEmpty(); }

	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	bool IsQuestActivated(FGameplayTag QuestTag) const { return ActivatedQuestTags.HasTag(QuestTag); }

	/**
	 * Enabled set: activated AND prereqs satisfied AND not blocked. The quests this giver can
	 * actually offer right now.
	 */
	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	const FGameplayTagContainer& GetEnabledQuests() const { return EnabledQuestTags; }

	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	bool IsQuestEnabled(FGameplayTag QuestTag) const { return EnabledQuestTags.HasTag(QuestTag); }

	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	bool CanGiveAnyQuests() const { return !EnabledQuestTags.IsEmpty(); }

	/** History of quests this giver has successfully given. Append-only within the session. */
	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	const FGameplayTagContainer& GetGivenQuests() const { return GivenQuestTags; }

	/** Returns the structured reasons (if any) why QuestTag can't currently be activated. */
	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	TArray<FQuestActivationBlocker> QueryActivationBlockers(FGameplayTag QuestTag) const;

	// ── Authored config accessors ────────────────────────────────

	UFUNCTION(BlueprintCallable)
	const FGameplayTagContainer& GetQuestTagsToGive() const { return QuestTagsToGive; }

	/**
	 * Registration-filtered view of QuestTagsToGive. Stale (unregistered) entries are dropped
	 * with a warning log; the authored container is unchanged. Safe to pass into
	 * FGameplayTagContainer::Filter / HasAny / MatchesAny without tripping UE's stale-tag ensure.
	 */
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetRegisteredQuestTagsToGive() const;

protected:
	virtual void PerformDeferredRegistration() override;

	virtual void InitializeComponent() override;

	/**
	 * Publishes this giver's QuestTagsToGive at InitializeComponent - before any BeginPlay - so the structural
	 * "this quest has a giver" set is populated before the questline's entry activation runs.
	 */
	void DeclareGiverQuests();

	/**
	 * Publishes one FQuestGiverRegisteredEvent, resolving the signal subsystem whether or not it's cached yet
	 * (the InitializeComponent declaration runs before BeginPlay caches it).
	 */
	void PublishGiverRegistration(FGameplayTag QuestTag);

	/** Quest tags this giver offers. Designer-authored on the placed component instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(Categories="SimpleQuest.Questline"))
	FGameplayTagContainer QuestTagsToGive;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="QuestGiver", meta=(AllowPrivateAccess=true))
	FGameplayTagContainer EnabledQuestTags;

	/**
	 * Quests in QuestTagsToGive that have reached this giver via the activation wire, regardless
	 * of prereq satisfaction. Broader than EnabledQuestTags.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "QuestGiver")
	FGameplayTagContainer ActivatedQuestTags;

	/** Quests this giver has successfully given. Appended in HandleQuestStarted. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "QuestGiver")
	FGameplayTagContainer GivenQuestTags;

	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove) override;

private:
	/**
	 * Every one of these is the Giver's OWN subscription on a tag in QuestTagsToGive, so no handler needs to ask
	 * whether the tag is one this component offers - the subscription set already answers that. The Giver owning
	 * both the state and the delegate is also what makes their ORDER a property of each function below rather
	 * than of subscription registration order.
	 */
	void OnQuestActivatedEventReceived   (FGameplayTag Channel, const FQuestActivatedEvent& Event);
	void OnQuestEnabledEventReceived     (FGameplayTag Channel, const FQuestEnabledEvent& Event);
	void OnQuestDisabledEventReceived    (FGameplayTag Channel, const FQuestDisabledEvent& Event);
	void OnQuestStartedEventReceived     (FGameplayTag Channel, const FQuestStartedEvent& Event);
	void OnQuestEndedEventReceived       (FGameplayTag Channel, const FQuestEndedEvent& Event);
	void OnQuestDeactivatedEventReceived (FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	void OnQuestGiveBlockedEventReceived (FGameplayTag Channel, const FQuestGiveBlockedEvent& Event);

	void RegisterQuestGiver();
	
	/**
	 * Installs the giver-specific subscriptions (Activated / Enabled / Disabled / Started / Ended / Deactivated,
	 * all ExactOnly) + the giver source entry for one quest, capturing the handles into the base
	 * SubscriptionHandlesByTag. Shared by registration and AddTagsToGive.
	 */
	void SubscribeGiverQuest(FGameplayTag QuestTag);

	/**
	 * Reconstructs Activated/Enabled state for a quest already pending-giver. Caller snapshots prior state and
	 * broadcasts. Shared by registration and AddTagsToGive.
	 */
	void GiverCatchUpForQuest(FGameplayTag QuestTag);

	/** Shared cleanup body for the deactivation and end-event paths. */
	void HandleQuestLeftGiverSurface(FGameplayTag Channel);

	/** Computes the delta between prior and current state and fires OnGiveAvailabilityChanged. */
	void BroadcastAvailabilityChange(const FGameplayTagContainer& PriorActivated, const FGameplayTagContainer& PriorEnabled, EGiveAvailabilityChangeReason Reason);

	/**
	 * Per-attempt blocker-event handles, keyed by quest tag. Populated by GiveQuest before the
	 * give request publishes; cleared when the cycle closes (Started or Blocked response).
	 * Presence of an entry signals "this giver has an in-flight give for this tag" - read by
	 * HandleQuestStarted to attribute the give to this giver.
	 */
	TMap<FGameplayTag, FDelegateHandle> PendingGiveBlockedHandles;

	void UnsubscribePendingGiveBlocked(FGameplayTag QuestTag);

	UQuestStateSubsystem* ResolveQuestStateSubsystem() const;

	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
};

