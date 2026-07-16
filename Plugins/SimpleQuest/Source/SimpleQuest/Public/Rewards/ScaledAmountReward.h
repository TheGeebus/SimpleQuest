// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Rewards/QuestRewardBase.h"
#include "ScaledAmountReward.generated.h"

/**
 * Reference reward: grants an amount scaled by the recipient's own state. The COMPUTED-FROM-EXTERNAL-CONTEXT pattern —
 * where LootTable computes from the incoming quest context, this computes from another game system, read off the
 * activation Instigator via IRewardScalingSource. The delivered amount is BaseAmount * the instigator's GetRewardScale()
 * (level curve, difficulty, prestige — whatever the game returns), so one reward node grants more to a higher-level
 * player with no per-level authoring.
 *
 * Offers an example of how to access external gameplay data at runtime from within the quest framework without coupling
 * it to other game systems.
 */
UCLASS(meta = (DisplayName = "Scaled Amount Reward"))
class SIMPLEQUEST_API UScaledAmountReward : public UQuestRewardBase
{
	GENERATED_BODY()

protected:
	/** What to grant (Experience, Currency.Gold, …). */
	UPROPERTY(EditAnywhere, meta = (Categories = "SimpleQuest.Reward"), Category = "Reward")
	FGameplayTag RewardType;

	/** Amount at scale 1.0. The delivered amount is this times the recipient's GetRewardScale(). */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Reward")
	int32 BaseAmount = 0;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
	virtual TArray<FQuestRewardPreview> DescribeReward_Implementation(const FQuestRewardPreviewContext& Context) const override;
};