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