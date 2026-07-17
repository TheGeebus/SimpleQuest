// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Rewards/QuestRewardBase.h"
#include "CurrencyReward.generated.h"

/**
 * Reference reward: grants a quantity of a currency. A PARAMETERIZED reward — the designer picks WHICH currency via the
 * Currency field (constrained to SimpleQuest.Reward.Currency.*: Gold, Gems, …) per instance. Same minimal shape as
 * XPReward; the only difference is the type is designer-chosen rather than fixed. Adopters define their own currency
 * tags under SimpleQuest.Reward.Currency.
 */
UCLASS(meta = (DisplayName = "Currency Reward"))
class SIMPLEQUEST_API UCurrencyReward : public UQuestRewardBase
{
	GENERATED_BODY()

protected:
	/** Which currency to grant. Picker constrained to the currency namespace. */
	UPROPERTY(EditAnywhere, meta = (Categories = "SimpleQuest.Reward.Currency"), Category = "Reward")
	FGameplayTag Currency;

	/** Quantity of the chosen currency granted. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Reward")
	int32 Amount = 0;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
	virtual TArray<FQuestRewardPreview> DescribeReward_Implementation(AActor* Viewer) const override;
};