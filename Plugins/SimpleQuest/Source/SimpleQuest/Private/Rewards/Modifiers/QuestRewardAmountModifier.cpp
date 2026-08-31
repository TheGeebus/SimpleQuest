// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/QuestRewardAmountModifier.h"

#include "Quests/Types/QuestRewardPayloads.h"
#include "SimpleQuestLog.h"
#include "Quests/Types/QuestRewardBlockerTags.h"


bool UQuestRewardAmountModifier::HandlesPayload(const UScriptStruct* PayloadType) const
{
	// Both forms of "an amount": the fixed one a grant carries, and the range a loot roll advertises.
	return PayloadType == FQuestRewardAmount::StaticStruct()
		|| PayloadType == FQuestRewardAmountRange::StaticStruct();
}

void UQuestRewardAmountModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	// The context arrives built - see UQuestRewardBase::DispatchDescribeReward, which synthesizes it once for every
	// modifier rather than leaving each to invent its own and disagree about what a preview knows.
	if (const FQuestRewardAmount* Fixed = Preview.PreviewData.GetPtr<FQuestRewardAmount>())
	{
		const int32 NewAmount = ModifyAmount(Fixed->Amount, AsIfActivating);
		Preview.PreviewData = FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ FMath::Max(NewAmount, 0) });

		// An amount modified away to nothing is still ADVERTISED, carrying its real zero. The grant drops it, and saying
		// so is more use to a player than a line that silently disappears - "this pays nothing right now" is an answer.
		if (NewAmount <= 0)
		{
			AddBlocker(Preview, TAG_RewardBlocker_NoValue.GetTag(),
				NSLOCTEXT("SimpleQuest", "RewardBlockedNoValue", "Currently worth nothing"));
		}
		return;
	}

	if (const FQuestRewardAmountRange* Range = Preview.PreviewData.GetPtr<FQuestRewardAmountRange>())
	{
		// BOTH ENDS, INDEPENDENTLY. A modifier is free to be non-linear - a clamp is - so scaling a midpoint and
		// re-deriving the ends would advertise a range the roll cannot actually produce.
		const int32 NewMin = ModifyAmount(Range->Min, AsIfActivating);
		const int32 NewMax = ModifyAmount(Range->Max, AsIfActivating);
		Preview.PreviewData = FInstancedStruct::Make<FQuestRewardAmountRange>(
			FQuestRewardAmountRange{ FMath::Max(NewMin, 0), FMath::Max(NewMax, 0) });

		if (NewMax <= 0)
		{
			AddBlocker(Preview, TAG_RewardBlocker_NoValue.GetTag(),
				NSLOCTEXT("SimpleQuest", "RewardBlockedNoValue", "Currently worth nothing"));
		}
	}
}

bool UQuestRewardAmountModifier::ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	// The payload gate in ApplyModifiers already ran, so this is expected to succeed. A null here means the contract
	// was bypassed rather than that the data is unusual - say so, because the two want different fixes.
	const FQuestRewardAmount* Existing = Grant.CustomData.GetPtr<FQuestRewardAmount>();
	if (!Existing)
	{
		// HandlesPayload accepts a range as well, because the advertised form of an amount is a range. A grant only
		// ever carries the fixed form, so reaching here means something delivered a range as an actual grant.
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("%s: a grant carried '%s' rather than a fixed amount - only the advertised form is a range. Grant left unchanged."),
			*GetClass()->GetName(),
			Grant.CustomData.GetScriptStruct() ? *Grant.CustomData.GetScriptStruct()->GetName() : TEXT("none"));
		return true;
	}

	const int32 NewAmount = ModifyAmount(Existing->Amount, Incoming);
	if (NewAmount <= 0)
	{
		UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("%s: amount modified to %d - grant dropped."), *GetClass()->GetName(), NewAmount);
		return false;
	}

	Grant.CustomData = FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ NewAmount });
	return true;
}

int32 UQuestRewardAmountModifier::ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming)
{
	return Amount;
}

