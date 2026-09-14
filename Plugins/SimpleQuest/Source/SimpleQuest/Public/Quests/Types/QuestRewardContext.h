// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestContextBase.h"
#include "QuestRewardContext.generated.h"

class AActor;

/**
 * The grant a recipient receives - published on the reward-type channel. The reward class supplies RewardType + the
 * payload (in the inherited CustomData) + an optional Recipient; the reward node fills the lineage (Instigator,
 * OriginTag, OriginChain, OriginatingEventID) from the activation context at publish time.
 *
 * Recipient identifies who the grant is FOR - valid = a targeted actor (self-filter), invalid = broadcast to every
 * subscriber of the type. Live-only (grants are never persisted), so deliberately NOT SaveGame-flagged.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestRewardContext : public FQuestContextBase
{
	GENERATED_BODY()

	/** The reward kind - the publish channel AND the recipient's branch key (e.g. SimpleQuest.Reward.Currency.Gold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag RewardType;
	/** Who the grant is for. The reward node defaults it to the activation Instigator; invalid = broadcast to all type-subscribers. */
	UPROPERTY(BlueprintReadWrite)
	TWeakObjectPtr<AActor> Recipient;

	/**
	 * Identity of the reward that produced this grant, copied from UQuestRewardBase::RewardGuid - the same value the
	 * ADVERTISEMENT stamps on FQuestRewardPreview. A UI that showed "complete this, get 50 XP" can match the payout when
	 * it arrives against the line that promised it, instead of guessing from a type and a number that two different
	 * rewards could both produce.
	 *
	 * Stamped with the rest of the lineage AFTER modifiers run, so a modifier cannot forge or clear it.
	 *
	 * PER REWARD, NOT PER GRANT: a reward calling DeliverReward more than once produces several grants carrying one GUID
	 * - which is the same granularity the preview side uses, so a multi-line advertisement and a multi-part payout
	 * correlate as a group rather than one to one.
	 *
	 * Live-only like the rest of this struct; grants are never persisted.
	 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RewardGuid;
};