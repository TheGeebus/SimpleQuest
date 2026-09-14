// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/Modifiers/QuestRewardAmountModifier.h"
#include "ScaleByRecipientModifier.generated.h"


/**
 * Reference modifier: multiplies an amount by the recipient's own scale, read through IRewardScalingSource. This is the
 * transform half of what Scaled Amount Reward used to do inside one class - and unlike that class it composes with any
 * source, so scaled loot is a loot table with this attached rather than a class nobody has written.
 *
 * An actor that does not implement the interface scales by one, so attaching this is safe before the game supplies a
 * value.
 */
UCLASS(meta = (DisplayName = "Scale By Recipient"))
class SIMPLEQUEST_API UScaleByRecipientModifier : public UQuestRewardAmountModifier
{
	GENERATED_BODY()

protected:
	virtual int32 ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming) override;
};

