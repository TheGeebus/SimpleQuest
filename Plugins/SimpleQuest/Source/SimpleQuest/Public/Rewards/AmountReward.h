// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Rewards/QuestRewardBase.h"
#include "AmountReward.generated.h"

/**
 * Reference reward: grants a fixed quantity of a chosen reward type. The plainest source there is - pick what, pick how
 * many - and the one to reach for when a modifier is going to do the interesting part.
 *
 * XP Reward and Currency Reward are narrower versions with their type fixed or constrained. Generic Reward is wider,
 * accepting any payload struct at the cost of authoring one. This sits between: any reward type, carrying the amount
 * payload every recipient already understands.
 */
UCLASS(meta = (DisplayName = "Amount Reward"))
class SIMPLEQUEST_API UAmountReward : public UQuestRewardBase
{
	GENERATED_BODY()

protected:
	/** What to grant (Experience, Currency.Gold, …). */
	UPROPERTY(EditAnywhere, meta = (Categories = "SimpleQuest.Reward"), Category = "Reward")
	FGameplayTag RewardType;

	/** How many, before modifiers. Anything in this reward's Modifiers list transforms it on the way out. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Reward")
	int32 Amount = 0;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
	virtual TArray<FQuestRewardPreview> DescribeReward_Implementation(AActor* Viewer) const override;
};

