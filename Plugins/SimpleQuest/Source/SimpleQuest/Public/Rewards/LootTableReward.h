// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/QuestRewardBase.h"
#include "LootTableReward.generated.h"

struct FQuestLootEntry;
class UQuestLootDataTable;
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

public:
#if WITH_EDITOR
	/**
	 * Editor diagnostic: describes a still-authored legacy reference, or an empty string when there is nothing to say.
	 * The judgement lives here rather than in the compiler because which of the two properties wins is this class's
	 * business - the compiler only needs to know whether there is something to report and what it should print.
	 */
	FString DescribeLegacyLootTableUse() const;
#endif

protected:
	/**
	 * Table of weighted drops to roll on. Soft ref - resolved (sync-loaded) at grant/describe time, so a questline
	 * carrying a loot reward doesn't force-load its table (and the table's transitive refs) as a hard dependency.
	 */
	UPROPERTY(EditAnywhere, Category = "Reward", meta = (DisplayName = "Loot Table"))
	TSoftObjectPtr<UQuestLootDataTable> LootDataTable;

	/**
	 * LEGACY - the data asset this reward used to read. Consulted only when Loot Table is empty, and warns when it is,
	 * so an existing questline keeps working untouched. Re-author its rows in a Loot Table asset and point Loot Table
	 * at it; this property and UQuestLootTable are both removed in 0.9.
	 *
	 * Stays EditAnywhere and stays an ordinary property on purpose: a designer has to SEE the old reference to know
	 * what needs re-authoring and to clear it afterwards, and marking it deprecated would stop it being saved at all.
	 */
	UPROPERTY(EditAnywhere, Category = "Reward", meta = (DisplayName = "Loot Table (legacy)"))
	TSoftObjectPtr<UQuestLootTable> LootTable;

	/** Number of independent rolls (with replacement - the same row can drop more than once). */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"), Category = "Reward")
	int32 RollCount = 1;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
	virtual TArray<FQuestRewardPreview> DescribeReward_Implementation(AActor* Viewer) const override;

private:
	/**
	 * Rows from whichever source is set, the DataTable first. Returns false when neither resolves. Both callers need
	 * the same resolution and the same ordering, and only the granting one reports problems - DescribeReward can be
	 * called every frame by a UI, so its failures stay silent rather than filling the log.
	 */
	bool GatherLootRows(TArray<const FQuestLootEntry*>& Out, bool bLogDiagnostics) const;
};