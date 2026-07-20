// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "QuestLootTable.generated.h"

/**
 * One weighted row in a loot table. When picked, grants a random amount in [MinAmount, MaxAmount] of RewardType. If
 * RequiredOutcome is set, the row is only eligible when the completion outcome that reached the reward matches it
 * (hierarchically) — this is how a table is parameterized off the completion path.
 */
USTRUCT(BlueprintType)
struct FQuestLootEntry
{
	GENERATED_BODY()

	/** What this row grants (Experience, Currency.Gold, …). Delivered on this channel like any other reward. */
	UPROPERTY(EditAnywhere, meta = (Categories = "SimpleQuest.Reward"), Category = "Loot")
	FGameplayTag RewardType;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Loot")
	int32 MinAmount = 1;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Loot")
	int32 MaxAmount = 1;

	/** Relative selection weight. Higher = more likely. Rows with weight <= 0 are skipped. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0"), Category = "Loot")
	float Weight = 1.0f;

	/** Optional gate: row is eligible only when the completion outcome matches this tag (empty = always eligible). */
	UPROPERTY(EditAnywhere, Category = "Loot")
	FGameplayTag RequiredOutcome;
};

/**
 * Reference data asset: a weighted table of loot rows. This is the "designer uses a DataAsset" path from the rewards
 * design — the reward UObject (ULootTableReward) reads this asset rather than hardcoding drops, so the loot content is
 * pure data an artist can edit without touching a reward class.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API UQuestLootTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Loot")
	TArray<FQuestLootEntry> Entries;
};