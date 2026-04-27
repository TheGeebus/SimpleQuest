// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Events/QuestStartedEvent.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "QuestGiverComponent.generated.h"


class UQuestStateSubsystem;
struct FQuestGiverRegisteredEvent;
struct FQuestDeactivatedEvent;
struct FQuestEnabledEvent;
struct FQuestActivatedEvent;
struct FQuestDisabledEvent;
struct FQuestGiveBlockedEvent;
struct FQuestActivationBlocker;
class UQuestManagerSubsystem;


/**
 * Fires when execution reaches this giver-gated quest. PrereqStatus describes whether prereqs are currently
 * satisfied — designers branch on PrereqStatus.bSatisfied to decide whether to show a "ready" or "locked" UI
 * affordance immediately.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestActivatedAtGiverDelegate, FGameplayTag, QuestTag, FQuestPrereqStatus, PrereqStatus);

/** Fires when a quest at this giver becomes accept-ready (Activated AND prereqs satisfy). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestEnabledAtGiverDelegate, FGameplayTag, QuestTag);

/**
 * Fires when an accept-ready quest at this giver becomes no-longer-accept-ready (sat → unsat transition, typically
 * NOT-prereq edge cases). Symmetric partner to OnQuestEnabled.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestDisabledAtGiverDelegate, FGameplayTag, QuestTag);

/**
 * Fires when an attempted give from this giver was refused by the manager. Carries the structured blocker array,
 * designer branches on Reason for contextual refusal dialogue.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestGiveBlockedAtGiverDelegate, FGameplayTag, QuestTag, const TArray<FQuestActivationBlocker>&, Blockers);
	
/**
 * Fires when a quest at this giver enters the Live state — typically right after a successful give. The
 * giver's state containers (ActivatedQuestTags / EnabledQuestTags) have been cleaned up by this point.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestStartedAtGiverDelegate,	FGameplayTag, QuestTag);

/**
 * Fires when a quest at this giver was deactivated before completing (abandon, cascade interrupt, SetBlocked).
 * Symmetric partner to OnQuestStarted for "this quest is no longer in the giver's purview" signalling.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestDeactivatedAtGiverDelegate,	FGameplayTag, QuestTag);


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestGiverComponent : public UQuestComponentBase
{
	GENERATED_BODY()

public:	
	UQuestGiverComponent();
	
	/** Fires when execution reaches a quest at this giver, regardless of prereq state. */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnQuestActivatedAtGiverDelegate OnQuestActivated;

	/** Fires when a quest becomes accept-ready at this giver. */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnQuestEnabledAtGiverDelegate OnQuestEnabled;

	/** Fires when an accept-ready quest at this giver becomes no-longer-ready. Rare; primarily NOT-prereq cases. */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnQuestDisabledAtGiverDelegate OnQuestDisabled;

	/** Fires when a give attempt from this giver was refused. Designer reads Blockers for contextual UI. */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnQuestGiveBlockedAtGiverDelegate OnQuestGiveBlocked;

	/** Fires when a quest at this giver starts (post-acceptance, quest now Live). */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnQuestStartedAtGiverDelegate OnQuestStarted;

	/** Fires when a quest at this giver is deactivated before completion. */
	UPROPERTY(BlueprintAssignable, Category = "QuestGiver|Lifecycle")
	FOnQuestDeactivatedAtGiverDelegate OnQuestDeactivated;
	
	/**
	 * Give the quest identified by QuestTag to this component's owner. Publishes FQuestGivenEvent on Tag_Channel_QuestGiven;
	 * UQuestManagerSubsystem picks it up, clears any PendingGiver state, stamps the merged params onto the target step, and
	 * routes into the normal activation pipeline (prereq / gate checks unchanged).
	 *
	 * The outgoing params are built by additively merging two sources, in this order:
	 *   1. This component's authored ActivationParams (baseline per placed instance).
	 *   2. The caller-supplied Params argument (per-call runtime data).
	 * Merge rules match UQuestStep::ActivateInternal: TargetActors / TargetClasses union, NumElementsRequired sums,
	 * and the single-valued fields (ActivationSource, CustomData, OriginTag, OriginChain) take the caller's value when
	 * set, otherwise the authored baseline. If neither source sets ActivationSource, it defaults to GetOwner() so
	 * objectives always have a "who activated me" reference.
	 *
	 * Blueprint: the Params pin is optional thanks to AutoCreateRefTerm — leave it unconnected and the authored
	 * ActivationParams (if any) carries alone. Wire a Make FQuestObjectiveActivationParams node to supply runtime data
	 * (dialogue choices, procedural targets, typed CustomData). 
	 *
	 * C++: pass an empty struct literal (or omit the argument) for the authored-only path; fill fields to add on top.
	 *
	 * @param QuestTag  Compiled quest tag identifying which step to activate. Must be a registered tag; no-op otherwise.
	 * @param Params    Optional per-call activation payload. Merged additively with the component's ActivationParams.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "Params"))
	void GiveQuestByTag(const FGameplayTag& QuestTag, const FQuestObjectiveActivationParams& Params = FQuestObjectiveActivationParams());
	
	/**
	 * BP-friendly wrapper for UQuestStateSubsystem::QueryQuestActivationBlockers. Returns empty array if the
	 * state subsystem can't be resolved (rare; world / GameInstance teardown).
	 */
	UFUNCTION(BlueprintCallable, Category = "QuestGiver")
	TArray<FQuestActivationBlocker> QueryActivationBlockers(FGameplayTag QuestTag) const;
	
protected:
	virtual void BeginPlay() override;
	
	/**
	 * New authoritative property — read by the manifest builder and eventually the sole registration mechanism once the
	 * full tag-based rework is complete.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(Categories="SimpleQuest.Quest"))
	FGameplayTagContainer QuestTagsToGive;

	/**
	 * Designer-authored activation payload published with every give. Full FQuestObjectiveActivationParams struct —
	 * placed-actor givers can pre-wire TargetActors (the specific enemies / items this giver's quest is about), counts,
	 * typed CustomData, etc. Merged additively with the step's authored defaults in UQuestStep::ActivateInternal. Empty
	 * (default-constructed) is the common case and incurs no behavior change vs. pre-Piece-C.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest")
	FQuestObjectiveActivationParams ActivationParams;
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Quest", meta=(Categories="SimpleQuest.Quest", AllowPrivateAccess=true))
	FGameplayTagContainer EnabledQuestTags;

	/**
	 * Quests that have reached this giver via the activation wire, regardless of prereq satisfaction. Compared to
	 * EnabledQuestTags — which only contains accept-ready quests — this is the broader "any quest in the giver's
	 * scope" set. Designers showing always-on indicators bind to this; designers showing only-when-actionable
	 * indicators bind to EnabledQuestTags or CanGiveAnyQuests.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "QuestGiver")
	FGameplayTagContainer ActivatedQuestTags;

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;

	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove) override;
	
private:
	void OnQuestActivatedEventReceived(FGameplayTag Channel, const FQuestActivatedEvent& Event);
	void OnQuestDisabledEventReceived(FGameplayTag Channel, const FQuestDisabledEvent& Event);
	void OnQuestGiveBlockedEventReceived(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event);

	/**
	 * One-shot blocker-event subscription handles, keyed by quest tag. Populated by GiveQuestByTag before the
	 * give event publishes; cleared on receipt of either FQuestGiveBlockedEvent or FQuestStartedEvent.
	 */
	TMap<FGameplayTag, FDelegateHandle> PendingGiveBlockedHandles;

	void UnsubscribePendingGiveBlocked(FGameplayTag QuestTag);

	UQuestStateSubsystem* ResolveQuestStateSubsystem() const;
	
	void RegisterQuestGiver();
	void OnQuestEnabledEventReceived(FGameplayTag Channel, const FQuestEnabledEvent& Event);
	void OnQuestStartedEventReceived(FGameplayTag Channel, const FQuestStartedEvent& Event);
	void OnQuestDeactivatedEventReceived(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

public:
	/**
	 * Returns the authored QuestTagsToGive container verbatim. May contain stale (unregistered) tags if the designer hasn't
	 * recompiled after removing a referenced node. Feed into tag-library Filter / HasAny / MatchesAny calls via
	 * GetRegisteredQuestTagsToGive() instead — raw stale entries assert inside UE's FGameplayTag::MatchesAny.
	 */
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetQuestTagsToGive() const { return QuestTagsToGive; }

	/**
	 * Registration-filtered view of QuestTagsToGive. Every returned tag is guaranteed registered in the runtime tag
	 * manager, making this safe to pass into FGameplayTagContainer::Filter / HasAny / MatchesAny. Stale entries are
	 * dropped (with a Warning log naming each one) but preserved on the underlying authored container.
	 */
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetRegisteredQuestTagsToGive() const;
	
	UFUNCTION(BlueprintCallable)
	bool CanGiveAnyQuests() const;
	UFUNCTION(BlueprintCallable)
	bool IsQuestEnabled(FGameplayTag QuestTag);

};
