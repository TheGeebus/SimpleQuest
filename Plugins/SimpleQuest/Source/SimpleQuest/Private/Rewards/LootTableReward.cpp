// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/LootTableReward.h"

#include "Rewards/QuestLootDataTable.h"
#include "Quests/Types/QuestRewardPayloads.h"
#include "StructUtils/InstancedStruct.h"
#include "SimpleQuestLog.h"

bool ULootTableReward::GatherLootRows(TArray<const FQuestLootEntry*>& Out, bool bLogDiagnostics) const
{
	static const FString Context(TEXT("ULootTableReward"));

	// Sync-load the soft reference at use time (designer authored a soft ref; the resident asset is only needed here).
	// The DataTable wins whenever it resolves, so pointing a converted reward at a real table is enough - the legacy
	// reference beneath it stops being consulted without having to be cleared first.
	if (const UQuestLootDataTable* Table = LootDataTable.LoadSynchronous())
	{
		// RowStruct is editable in the DataTable editor, so a table pointed at the wrong struct would otherwise read as
		// simply empty. Name both structs - the message has to be actionable from the log alone.
		if (Table->GetRowStruct() != FQuestLootEntry::StaticStruct())
		{
			UE_LOG(LogSimpleQuestActivation, Warning, TEXT("ULootTableReward: '%s' has row struct '%s', expected '%s' — no rows read."),
				*Table->GetName(), Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("none"),
				*FQuestLootEntry::StaticStruct()->GetName());
			return false;
		}

		// A DataTable keeps its rows in a TMap, whose iteration order carries no promise. Walking sorted row names
		// makes a seeded roll reproducible and keeps a preview list in the same order between sessions.
		TArray<FName> RowNames;
		Table->GetRowMap().GetKeys(RowNames);
		RowNames.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

		Out.Reserve(RowNames.Num());
		for (const FName& RowName : RowNames)
		{
			if (const FQuestLootEntry* Row = Table->FindRow<FQuestLootEntry>(RowName, Context, /*bWarnIfRowMissing*/ false))
			{
				Out.Add(Row);
			}
		}
		return true;
	}

	if (bLogDiagnostics)
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("ULootTableReward: no Loot Table assigned — nothing granted."));
	}
	return false;
}

void ULootTableReward::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	TArray<const FQuestLootEntry*> Rows;
	if (!GatherLootRows(Rows, /*bLogDiagnostics*/ true))
	{
		return;
	}

	// Build the eligible set for THIS activation: positive-weight rows whose RequiredOutcome (if any) matches the
	// completion outcome that reached this reward. Filtering on Incoming.IncomingOutcomeTag is the context hook - the
	// same table yields different drops depending on how the quest was completed.
	TArray<const FQuestLootEntry*> Eligible;
	float TotalWeight = 0.f;
	for (const FQuestLootEntry* Entry : Rows)
	{
		if (Entry->Weight <= 0.f) continue;
		if (Entry->RequiredOutcome.IsValid() && !Incoming.IncomingOutcomeTag.MatchesTag(Entry->RequiredOutcome)) continue;
		Eligible.Add(Entry);
		TotalWeight += Entry->Weight;
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

TArray<FQuestRewardPreview> ULootTableReward::DescribeReward_Implementation(AActor* Viewer) const
{
	TArray<FQuestRewardPreview> Out;
	TArray<const FQuestLootEntry*> Rows;
	if (!GatherLootRows(Rows, false)) return Out;
	for (const FQuestLootEntry* E : Rows)
	{
		if (E->Weight <= 0.f) continue;
		FQuestRewardPreview P;
		P.RewardType  = E->RewardType;
		P.PreviewData = FInstancedStruct::Make<FQuestRewardAmountRange>(
			FQuestRewardAmountRange{ FMath::Min(E->MinAmount, E->MaxAmount), FMath::Max(E->MinAmount, E->MaxAmount) });
		Out.Add(P);
		Out.Add(P);
	}
	return Out;
}

