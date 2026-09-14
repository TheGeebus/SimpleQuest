// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/ScaleAmountModifier.h"


int32 UScaleAmountModifier::ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming)
{
	return FMath::RoundToInt(Amount * Multiplier);
}

