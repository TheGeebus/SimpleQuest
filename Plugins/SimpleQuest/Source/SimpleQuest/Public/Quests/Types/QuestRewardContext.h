// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestContextBase.h"
#include "QuestRewardContext.generated.h"

class AActor;

/**
 * The grant a recipient receives — published on the reward-type channel. The reward class supplies RewardType + the
 * payload (in the inherited CustomData) + an optional Recipient; the reward node fills the lineage (Instigator,
 * OriginTag, OriginChain, OriginatingEventID) from the activation context at publish time.
 *
 * Recipient identifies who the grant is FOR — valid = a targeted actor (self-filter), invalid = broadcast to every
 * subscriber of the type. Live-only (grants are never persisted), so deliberately NOT SaveGame-flagged.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestRewardContext : public FQuestContextBase
{
	GENERATED_BODY()

	/** The reward kind — the publish channel AND the recipient's branch key (e.g. SimpleQuest.Reward.Currency.Gold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag RewardType;

	/** Who the grant is for. The reward node defaults it to the activation Instigator; invalid = broadcast to all type-subscribers. */
	UPROPERTY(BlueprintReadWrite)
	TWeakObjectPtr<AActor> Recipient;
};