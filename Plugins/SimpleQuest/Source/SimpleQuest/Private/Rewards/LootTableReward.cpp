// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/LootTableReward.h"

#include "Rewards/QuestLootTable.h"
#include "Quests/Types/QuestRewardPayloads.h"
#include "StructUtils/InstancedStruct.h"
#include "SimpleQuestLog.h"

void ULootTableReward::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	if (!LootTable)
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("ULootTableReward: no LootTable assigned — nothing granted."));
		return;
	}

	// Build the eligible set for THIS activation: positive-weight rows whose RequiredOutcome (if any) matches the
	// completion outcome that reached this reward. Filtering on Incoming.IncomingOutcomeTag is the context hook — the
	// same table yields different drops depending on how the quest was completed.
	TArray<const FQuestLootEntry*> Eligible;
	float TotalWeight = 0.f;
	for (const FQuestLootEntry& Entry : LootTable->Entries)
	{
		if (Entry.Weight <= 0.f) continue;
		if (Entry.RequiredOutcome.IsValid() && !Incoming.IncomingOutcomeTag.MatchesTag(Entry.RequiredOutcome)) continue;
		Eligible.Add(&Entry);
		TotalWeight += Entry.Weight;
	}

	if (Eligible.Num() == 0 || TotalWeight <= 0.f)
	{
		UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ULootTableReward: no eligible rows for outcome '%s' — nothing granted."),
			*Incoming.IncomingOutcomeTag.ToString());
		return;
	}

	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ULootTableReward: rolling %d time(s) over %d eligible row(s) (outcome '%s')."),
		RollCount, Eligible.Num(),
		*Incoming.IncomingOutcomeTag.ToString());

	// One weighted pick per roll; each queues an independent grant. RollCount > 1 is what exercises the node's
	// collect-all-pending-grants path with more than one grant.
	for (int32 Roll = 0; Roll < RollCount; ++Roll)
	{
		float Pick = FMath::FRandRange(0.f, TotalWeight);
		const FQuestLootEntry* Chosen = Eligible.Last();   // guards float drift leaving Pick just above zero at the top
		for (const FQuestLootEntry* Entry : Eligible)
		{
			Pick -= Entry->Weight;
			if (Pick <= 0.f) { Chosen = Entry; break; }
		}

		const int32 Amount = FMath::RandRange(FMath::Min(Chosen->MinAmount, Chosen->MaxAmount), FMath::Max(Chosen->MinAmount, Chosen->MaxAmount));
		if (Amount <= 0) continue;

		DeliverReward(Chosen->RewardType, FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ Amount }));
	}
}

TArray<FQuestRewardPreview> ULootTableReward::DescribeReward_Implementation(const FQuestRewardPreviewContext& Context) const
{
	TArray<FQuestRewardPreview> Out;
	if (!LootTable) return Out;
	for (const FQuestLootEntry& E : LootTable->Entries)
	{
		if (E.Weight <= 0.f) continue;
		FQuestRewardPreview P;
		P.RewardType  = E.RewardType;
		P.PreviewData = FInstancedStruct::Make<FQuestRewardAmountRange>(
			FQuestRewardAmountRange{ FMath::Min(E.MinAmount, E.MaxAmount), FMath::Max(E.MinAmount, E.MaxAmount) });
		Out.Add(P);
	}
	return Out;
}
