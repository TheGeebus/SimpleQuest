// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/QuestReward.h"
#include "CurrencyReward.generated.h"

/**
 * Reference reward: grants a quantity of a currency. A PARAMETERIZED reward — the designer picks WHICH currency by
 * setting RewardType to a SimpleQuest.Reward.Currency.* tag (Gold, Gems, …) per instance. Same minimal shape as
 * XPReward; the only difference is the type is designer-chosen rather than fixed. Adopters define their own currency
 * tags under SimpleQuest.Reward.Currency.
 */
UCLASS(meta = (DisplayName = "Currency Reward"))
class SIMPLEQUEST_API UCurrencyReward : public UQuestReward
{
	GENERATED_BODY()

protected:
	/** Quantity of the chosen currency granted. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Reward")
	int32 Amount = 0;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
};