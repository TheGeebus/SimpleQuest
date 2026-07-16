// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestRewardPayloads.generated.h"

/**
 * Payload for an integer-amount reward (XP points, a quantity of currency, …). The reward packs it into
 * FQuestRewardContext::CustomData; a recipient reads it via Grant.CustomData.Get<FQuestRewardAmount>(). The RewardType
 * tag says WHAT the amount is (Experience vs Currency.Gold); this struct just carries the number. It's the wire
 * contract both sides agree on — the reference rewards below and the recipient share it.
 */
USTRUCT(BlueprintType)
struct FQuestRewardAmount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Amount = 0;
};

/**
 * Payload for a reward whose amount is a RANGE, not a fixed quantity — a loot roll's possible drop, a "10-25 gold"
 * preview. Packed by LootTable's DescribeReward; a UI reads it via PreviewData.Get<FQuestRewardAmountRange>(). The struct
 * TYPE is the "not a fixed amount" signal (vs FQuestRewardAmount for a definite value).
 */
USTRUCT(BlueprintType)
struct FQuestRewardAmountRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Min = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Max = 0;
};