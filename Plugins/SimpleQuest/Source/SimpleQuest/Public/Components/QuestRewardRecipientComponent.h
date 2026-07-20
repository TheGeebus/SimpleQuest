// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/QuestComponentBase.h"
#include "Quests/Types/QuestRewardContext.h"
#include "Signals/Types/SignalRoutingFlags.h"
#include "QuestRewardRecipientComponent.generated.h"

struct FQuestRewardGrantedEvent;

/**
 * Per-reward-type subscription settings — one entry per reward-type channel the component reacts to.
 */
USTRUCT(BlueprintType)
struct FRewardTypeSubscription
{
	GENERATED_BODY()

	/**
	 * How this reward-type channel is matched. Descendants (default) = hierarchical: SimpleQuest.Reward.Currency also
	 * catches Currency.Gold / Currency.Gems / … . ExactMatch = only grants published on this exact tag. Ancestors /
	 * Bidirectional are reserved — the Ancestors walk is a no-op until the bus's publish-side descendant-walk lands.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESignalRoutingMode Routing = FSignalRoutingDefaults::HierarchicalSubscribe;
};

/**
 * Receives quest reward grants and turns them into feedback. Drop on any actor that should react to rewards — the
 * player's stats/inventory, an NPC whose faction shifts, a HUD, a world object. Declare the reward-type channels it
 * cares about in ReactsToRewardTypes (hierarchical — SimpleQuest.Reward.Currency catches ...Currency.Gold); on a
 * matching grant the component self-filters via DoesRewardTargetMe and broadcasts OnRewardGranted.
 *
 * The framework doesn't define what "feedback" is: bind OnRewardGranted and do whatever the grant means for this
 * actor — add to a wallet, bump XP, toast the HUD, play a sound. Grants are live-only (never replayed on save
 * restore or late-join), so a received grant is always a real, first-time grant — no delivery gating needed.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SIMPLEQUEST_API UQuestRewardRecipientComponent : public UQuestComponentBase
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRewardGranted, FQuestRewardContext, Grant);

	/**
	 * Fires when a grant this component cares about (subscribed type + passes DoesRewardTargetMe) arrives. Read
	 * Grant.RewardType to branch, Grant.CustomData.Get<FYourPayload>() for the payload, then apply the effect.
	 */
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnRewardGranted OnRewardGranted;

	/**
	 * Reward-type channels this actor reacts to, each with its own routing mode. A parent type on Descendants (the
	 * default for new entries) catches its whole subtree; ExactMatch narrows to the exact type. Keys are picked from
	 * the SimpleQuest.Reward namespace.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward", meta = (Categories = "SimpleQuest.Reward"))
	TMap<FGameplayTag, FRewardTypeSubscription> ReactsToRewardTypes;

	/**
	 * "Is this grant for me?" A broadcast grant (no explicit recipient) reaches everyone subscribed to the type; a
	 * targeted grant reaches the component whose owner IS the recipient, or whose owner is owned by the recipient
	 * (e.g. a controller targeting its pawn). Override to extend — this is the seam the multiplayer scope system
	 * plugs into (matching on scope context) without touching the rest of the receive path.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Reward")
	bool DoesRewardTargetMe(const FQuestRewardContext& Grant) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Bus handler for a grant on a subscribed reward-type channel. Self-filters via DoesRewardTargetMe, then broadcasts. */
	void HandleRewardGranted(FGameplayTag Channel, const FQuestRewardGrantedEvent& Event);
};