// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/QuestRewardModifier.h"

#include "SimpleQuestLog.h"
#include "Rewards/RewardScalingSource.h"

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

bool UQuestRewardModifier::DispatchModifyPreview(FQuestRewardPreview& Preview, AActor* Viewer)
{
	return ModifyPreview(Preview, Viewer);
}

bool UQuestRewardModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, AActor* Viewer)
{
	// Pass-through, like ModifyGrant. A modifier that only cares about grants leaves the advertisement alone rather
	// than hiding it - silence here would make a reward vanish from "do this, get this" for no stated reason.
	return true;
}

bool UQuestRewardAmountModifier::HandlesPayload(const UScriptStruct* PayloadType) const
{
	// Both forms of "an amount": the fixed one a grant carries, and the range a loot roll advertises.
	return PayloadType == FQuestRewardAmount::StaticStruct()
		|| PayloadType == FQuestRewardAmountRange::StaticStruct();
}

bool UQuestRewardAmountModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, AActor* Viewer)
{
	// ModifyAmount takes an activation context because that is what the grant path holds. A preview has a Viewer and
	// nothing else: nothing activated, so Provenance stays Unknown and no outcome routed here. The Instigator IS the
	// viewer - both mean "the actor this reward is about" - and a subclass therefore implements ONE function for both
	// paths rather than two that can disagree.
	FQuestRewardActivationContext AsIfActivating;
	AsIfActivating.Instigator = Viewer;

	if (const FQuestRewardAmount* Fixed = Preview.PreviewData.GetPtr<FQuestRewardAmount>())
	{
		const int32 NewAmount = ModifyAmount(Fixed->Amount, AsIfActivating);
		if (NewAmount <= 0)
		{
			return false;
		}
		Preview.PreviewData = FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ NewAmount });
		return true;
	}

	if (const FQuestRewardAmountRange* Range = Preview.PreviewData.GetPtr<FQuestRewardAmountRange>())
	{
		// BOTH ENDS, INDEPENDENTLY. A modifier is free to be non-linear - a clamp is - so scaling a midpoint and
		// re-deriving the ends would advertise a range the roll cannot actually produce.
		const int32 NewMin = ModifyAmount(Range->Min, AsIfActivating);
		const int32 NewMax = ModifyAmount(Range->Max, AsIfActivating);
		if (NewMax <= 0)
		{
			return false;
		}
		Preview.PreviewData = FInstancedStruct::Make<FQuestRewardAmountRange>(
			FQuestRewardAmountRange{ FMath::Max(NewMin, 0), NewMax });
		return true;
	}

	return true;
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

int32 UClampAmountModifier::ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming)
{
	// Bounds authored the wrong way round are a data mistake, not a reason to grant nothing - order them rather than
	// clamping into an empty range and silently dropping every grant that passes through.
	return FMath::Clamp(Amount, FMath::Min(MinAmount, MaxAmount), FMath::Max(MinAmount, MaxAmount));
}

int32 UScaleByRecipientModifier::ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming)
{
	// Reach the game through the Instigator rather than this modifier's own GetWorld(), which is null: a modifier is a
	// subobject of a reward, which belongs to a questline graph ASSET, and an asset has no world. In the advertisement
	// path the amount layer puts the VIEWER here, so this same line answers "what would I get" for whoever is asking.
	AActor* Actor = Incoming.Instigator.Get();
	if (!Actor || !Actor->Implements<URewardScalingSource>())
	{
		return Amount;
	}

	return FMath::RoundToInt(Amount * IRewardScalingSource::Execute_GetRewardScale(Actor));
}

