// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/QuestRewardModifier.h"

#include "SimpleQuestLog.h"

bool UQuestRewardModifier::DispatchModifyGrant(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	// Through the UFunction thunk rather than calling _Implementation directly, so a Blueprint override actually runs.
	return ModifyGrant(Grant, Incoming);
}

bool UQuestRewardModifier::ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	// Pass-through rather than pure virtual: a modifier that does nothing is legal (if useless), and a Blueprint subclass
	// that forgets to override should not take the grant down with it.
	return true;
}

bool UQuestRewardAmountModifier::ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	// The payload gate in ApplyModifiers already ran, so this is expected to succeed. A null here means the contract
	// was bypassed rather than that the data is unusual - say so, because the two want different fixes.
	const FQuestRewardAmount* Existing = Grant.CustomData.GetPtr<FQuestRewardAmount>();
	if (!Existing)
	{
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("%s: reached an amount modifier with a non-amount payload - the payload gate did not run. Grant left unchanged."),
			*GetClass()->GetName());
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

int32 UClampAmountModifier::ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming)
{
	// Bounds authored the wrong way round are a data mistake, not a reason to grant nothing - order them rather than
	// clamping into an empty range and silently dropping every grant that passes through.
	return FMath::Clamp(Amount, FMath::Min(MinAmount, MaxAmount), FMath::Max(MinAmount, MaxAmount));
}

