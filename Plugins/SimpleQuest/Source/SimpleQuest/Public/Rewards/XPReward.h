// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/QuestReward.h"
#include "XPReward.generated.h"

/**
 * Reference reward: grants a fixed amount of experience. A FIXED-TYPE reward — it always grants
 * SimpleQuest.Reward.Experience (no type field to set). Shows the minimal typed-subclass pattern: add a typed field
 * (Amount), override TryGrantReward to pack it into a payload struct, and call DeliverReward.
 */
UCLASS(meta = (DisplayName = "XP Reward"))
class SIMPLEQUEST_API UXPReward : public UQuestReward
{
	GENERATED_BODY()

protected:
	/** Experience points granted. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Reward")
	int32 Amount = 0;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
};