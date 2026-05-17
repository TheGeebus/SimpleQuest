// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestTriggerComponent.h"
#include "Components/ActorComponent.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
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
 * on Reason for differentiated UI treatment — e.g., "newly available" pulse vs. "lost availability"
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
 * convenience — designer doesn't need a follow-up GetActivatedQuests() / GetEnabledQuests()
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
 * Component for actors that offer quests to the player. Configure QuestTagsToGive with the
 * quest tags this giver should offer; the component automatically tracks Activated / Enabled /
 * Given state for those quests via the lifecycle event bus. Designer refreshes UI from
 * OnGiveAvailabilityChanged plus the state queries (GetEnabledQuests / CanGiveAnyQuests / etc.)
 * and triggers gives via GiveQuest / GiveAllQuests.
 *
 * Per-give success and refusal notifications come from the inherited UQuestObserverComponent
 * delegates OnQuestStarted (Live transition with GiverActor populated) and OnQuestGiveBlocked
 * (refusal with Blockers and GiverActor). QuestTagsToGive entries are implicitly observed —
 * adopters can bind those delegates without authoring a parallel ObservedTags entry. Filter by
 * GiverActor == GetOwner() to scope to this giver's attempts.
 *
 * Inherits the full observation surface from UQuestObserverComponent and the trigger /
 * Send-event surface from UQuestTriggerComponent. A single Giver component can offer quests
 * AND act as a trigger target for other quests' step objectives — populate both
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


	// ── Action API ───────────────────────────────────────────────

	/**
	 * Give a specific quest. Publishes a request to the manager, which clears any PendingGiver
	 * state, stamps Context onto the target step, and routes into the normal activation pipeline.
	 * The outcome surfaces asynchronously via the inherited OnQuestStarted (success) or
	 * OnQuestGiveBlocked (refusal) delegates; filter by GiverActor == GetOwner() to scope to this
	 * giver's attempts. If Context.Dynamic.Instigator is unset, it defaults to this giver's
	 * owning actor.
	 *
	 * @param QuestTag   The quest to give. Must be registered in the runtime tag manager.
	 * @param Context    Per-call activation context. Empty default carries no extra data.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "Context"))
	void GiveQuest(const FGameplayTag& QuestTag, const FQuestObjectiveActivationContext& Context = FQuestObjectiveActivationContext());

	/**
	 * Give every currently-enabled quest in QuestTagsToGive. Iterates in authored order and
	 * calls GiveQuest on each. The same Context applies to all. Useful for "interact with NPC,
	 * give everything available" interaction patterns where designer doesn't want to pick
	 * individually.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "Context"))
	void GiveAllQuests(const FQuestObjectiveActivationContext& Context = FQuestObjectiveActivationContext());

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
	virtual void BeginPlay() override;

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

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;
	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove) override;

	virtual FGameplayTagContainer GetImplicitlyObservedTags() const override;
	
	/**
	 * The three overrides below all interleave Giver-specific state tracking with the inherited Observer
	 * broadcast. Pattern: filter to QuestTagsToGive → state update → Super::HandleQuest*(broadcast) →
	 * BroadcastAvailabilityChange. State runs BEFORE Super so BP listeners bound to the inherited
	 * delegates see fully-updated GivenQuestTags / ActivatedQuestTags / EnabledQuestTags when they query
	 * inside the callback. Each override replaces a previous separate Giver subscription whose dispatch
	 * order ran AFTER Observer's broadcast (Observer subscriptions register in Super::BeginPlay; Giver's
	 * separate ones registered later in RegisterQuestGiver, putting them downstream in dispatch order).
	 *
	 * Activated / Disabled / Deactivated keep their separate Giver subscriptions because Observer doesn't
	 * subscribe to those events by default — moving them into overrides would silently lose state-tracking
	 * for tags only in QuestTagsToGive (not in ObservedTags). The subscriber-order issue only manifests
	 * for those events when a designer explicitly opts in via ObservedTags with the respective flag true,
	 * which is the narrow case we accept as a known limitation.
	 */
	virtual void HandleQuestEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event) override;
	virtual void HandleQuestStarted(FGameplayTag Channel, const FQuestStartedEvent& Event) override;
	virtual void HandleQuestCompleted(FGameplayTag Channel, const FQuestEndedEvent& Event) override;

private:
	void OnQuestActivatedEventReceived   (FGameplayTag Channel, const FQuestActivatedEvent& Event);
	void OnQuestDisabledEventReceived    (FGameplayTag Channel, const FQuestDisabledEvent& Event);
	void OnQuestDeactivatedEventReceived (FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	void OnQuestGiveBlockedEventReceived (FGameplayTag Channel, const FQuestGiveBlockedEvent& Event);

	void RegisterQuestGiver();

	/** Shared cleanup body for the deactivation and end-event paths. */
	void HandleQuestLeftGiverSurface(FGameplayTag Channel);

	/** Computes the delta between prior and current state and fires OnGiveAvailabilityChanged. */
	void BroadcastAvailabilityChange(const FGameplayTagContainer& PriorActivated, const FGameplayTagContainer& PriorEnabled, EGiveAvailabilityChangeReason Reason);

	/**
	 * Per-attempt blocker-event handles, keyed by quest tag. Populated by GiveQuest before the
	 * give request publishes; cleared when the cycle closes (Started or Blocked response).
	 * Presence of an entry signals "this giver has an in-flight give for this tag" — read by
	 * HandleQuestStarted to attribute the give to this giver.
	 */
	TMap<FGameplayTag, FDelegateHandle> PendingGiveBlockedHandles;

	void UnsubscribePendingGiveBlocked(FGameplayTag QuestTag);

	UQuestStateSubsystem* ResolveQuestStateSubsystem() const;

	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
};