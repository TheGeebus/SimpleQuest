// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/LootTableReward.h"

#include "Rewards/QuestLootDataTable.h"
#include "Rewards/QuestLootTable.h"
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

	if (const UQuestLootTable* Legacy = LootTable.LoadSynchronous())
	{
		if (bLogDiagnostics)
		{
			UE_LOG(LogSimpleQuestActivation, Warning,
				TEXT("ULootTableReward: reading deprecated Quest Loot Table '%s'. Re-author its rows as a Loot Table asset — the data asset form is removed in 0.9."),
				*Legacy->GetName());
		}

		// Authored order, which the array already carried; the sorted walk above exists to match it.
		Out.Reserve(Legacy->Entries.Num());
		for (const FQuestLootEntry& Entry : Legacy->Entries)
		{
			Out.Add(&Entry);
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

#if WITH_EDITOR
FString ULootTableReward::DescribeLegacyLootTableUse() const
{
	if (LootTable.IsNull())
	{
		return FString();
	}

	// Two different problems wearing the same symptom. With no Loot Table assigned the deprecated asset is what
	// actually gets rolled; with one assigned it is dead weight somebody forgot to clear. Both want clearing, but only
	// the first stops granting anything in 0.9, so the two must not read the same.
	return LootDataTable.IsNull()
		? FString::Printf(TEXT("rolls the deprecated Quest Loot Table '%s' - re-author its rows as a Loot Table asset "
			"and assign it, because the data asset form is removed in 0.9"), *LootTable.ToString())
		: FString::Printf(TEXT("still references the deprecated Quest Loot Table '%s', which is ignored because a Loot "
			"Table is assigned - clear it, because the data asset form is removed in 0.9"), *LootTable.ToString());
}
#endif
