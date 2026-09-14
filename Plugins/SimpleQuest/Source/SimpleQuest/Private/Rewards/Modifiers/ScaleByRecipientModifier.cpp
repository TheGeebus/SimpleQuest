// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/ScaleByRecipientModifier.h"

#include "GameFramework/Actor.h"
#include "Rewards/RewardScalingSource.h"


int32 UScaleByRecipientModifier::ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming)
{
	// Reach the game through the Instigator rather than this modifier's own GetWorld(), which is null: a modifier is a
	// subobject of a reward, which belongs to a questline graph ASSET, and an asset has no world. In the advertisement
	// path the reward's own preview pass puts the VIEWER here, so this same line answers "what would I get" for whoever
	// is asking.
	AActor* Actor = Incoming.Instigator.Get();
	if (!Actor || !Actor->Implements<URewardScalingSource>())
	{
		return Amount;
	}

	return FMath::RoundToInt(Amount * IRewardScalingSource::Execute_GetRewardScale(Actor));
}

