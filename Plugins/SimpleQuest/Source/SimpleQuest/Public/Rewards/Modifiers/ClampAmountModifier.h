// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/Modifiers/QuestRewardAmountModifier.h"
#include "ClampAmountModifier.generated.h"


/**
 * Reference modifier: clamps an amount into [MinAmount, MaxAmount]. Deliberately the simplest useful one - it exists to
 * demonstrate the layer and to give the order-matters case something concrete to test with, since scale-then-clamp and
 * clamp-then-scale produce different numbers from the same two modifiers.
 */
UCLASS(meta = (DisplayName = "Clamp Amount"))
class SIMPLEQUEST_API UClampAmountModifier : public UQuestRewardAmountModifier
{
	GENERATED_BODY()

	/** Tests author the bounds the way a designer would; see FQuestRewardModifierTestAccess. */
	friend class FQuestRewardModifierTestAccess;

protected:
	/** Lower bound. A grant below this is raised to it; set both bounds equal to force a fixed amount. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Modifier")
	int32 MinAmount = 0;

	/** Upper bound. A grant above this is lowered to it. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Modifier")
	int32 MaxAmount = 1000;

	virtual int32 ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming) override;
};

