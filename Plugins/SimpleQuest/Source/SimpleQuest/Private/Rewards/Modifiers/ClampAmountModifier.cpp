// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/ClampAmountModifier.h"


int32 UClampAmountModifier::ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming)
{
	// Bounds authored the wrong way round are a data mistake, not a reason to grant nothing - order them rather than
	// clamping into an empty range and silently dropping every grant that passes through.
	return FMath::Clamp(Amount, FMath::Min(MinAmount, MaxAmount), FMath::Max(MinAmount, MaxAmount));
}

