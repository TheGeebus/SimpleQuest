// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/Modifiers/QuestRewardAmountModifier.h"
#include "ScaleAmountModifier.generated.h"


/**
 * Reference modifier: multiplies an amount by an authored constant. The companion to Scale By Recipient, which reads its
 * factor off the recipient through an interface - this one is the case where the number is simply a design decision:
 * a double-XP event, a harder difficulty paying more, one placement of a shared questline being worth less than another.
 *
 * ROUNDS TO THE NEAREST WHOLE NUMBER, and a result of zero DROPS the grant, per the amount layer's rule that a reward
 * worth nothing is worth not publishing. Rounding is half-up, so halving a 1 still pays 1; it takes a small enough
 * multiplier to round a grant away entirely, and that is a real outcome rather than a paid zero.
 */
UCLASS(meta = (DisplayName = "Scale Amount"))
class SIMPLEQUEST_API UScaleAmountModifier : public UQuestRewardAmountModifier
{
	GENERATED_BODY()

	friend class FQuestRewardModifierTestAccess;

protected:
	/**
	 * What the amount is multiplied by. 1 leaves it alone; below 1 shrinks it. Order in the Modifiers array is meaning -
	 * a cap listed before this one is applied to the UNSCALED number.
	 */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0"), Category = "Modifier")
	float Multiplier = 1.0f;

	virtual int32 ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming) override;
};

