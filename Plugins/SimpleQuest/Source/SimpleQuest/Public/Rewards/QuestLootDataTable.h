// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"
#include "QuestLootDataTable.generated.h"

/**
 * One weighted row in a loot table. When picked, grants a random amount in [MinAmount, MaxAmount] of RewardType. If
 * RequiredOutcome is set, the row is only eligible when the completion outcome that reached the reward matches it
 * (hierarchically) - this is how a table is parameterized off the completion path.
 */
USTRUCT(BlueprintType)
struct FQuestLootEntry : public FTableRowBase
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
 * A weighted table of loot. ULootTableReward rolls one rather than hardcoding rewards, so loot content is pure data an
 * artist edits without touching a reward class.
 *
 * It is a DataTable because it is a table: rows edit in the row editor, import and export as CSV or JSON, and are
 * visible to the data resolver as an ordinary source. Subclassing rather than using UDataTable directly keeps the
 * reference on ULootTableReward type-safe - the picker offers loot tables, not every table in the project - and lets
 * the row struct be fixed here, so creating one never asks which struct its rows are.
 *
 * Supersedes UQuestLootTable, the data asset this started as, which survives one release so existing assets keep
 * loading. The class is named for what it is rather than what it is called, because the old one still holds the good
 * name until 0.9 removes it; both present as "Quest Loot Table" in the browser.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API UQuestLootDataTable : public UDataTable
{
	GENERATED_BODY()

public:
	UQuestLootDataTable()
	{
		RowStruct = FQuestLootEntry::StaticStruct();
	}
};

