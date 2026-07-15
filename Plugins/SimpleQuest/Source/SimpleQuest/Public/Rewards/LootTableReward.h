// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/QuestRewardBase.h"
#include "LootTableReward.generated.h"

class UQuestLootTable;

/**
 * Reference reward: rolls a loot table and grants the results. The COMPUTED path — unlike XPReward / CurrencyReward
 * (fixed shape), the type AND amount are decided at grant time by a weighted roll, optionally filtered by the
 * completion outcome. Each roll queues its own grant, so one loot reward can produce several grants in a single
 * activation. It reuses FQuestRewardAmount and delivers on the same type channels, so recipients built for XP or
 * currency handle loot drops with no extra code.
 */
UCLASS(meta = (DisplayName = "Loot Table Reward"))
class SIMPLEQUEST_API ULootTableReward : public UQuestRewardBase
{
	GENERATED_BODY()

protected:
	/** Table of weighted drops to roll on. */
	UPROPERTY(EditAnywhere, Category = "Reward")
	TObjectPtr<UQuestLootTable> LootTable;

	/** Number of independent rolls (with replacement — the same row can drop more than once). */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"), Category = "Reward")
	int32 RollCount = 1;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
};