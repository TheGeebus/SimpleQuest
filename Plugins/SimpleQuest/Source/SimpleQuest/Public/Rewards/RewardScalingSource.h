// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RewardScalingSource.generated.h"

/**
 * Reference interface: a recipient that can tell a reward how to scale its grant. Implement it on whatever receives
 * scaled rewards (a player character, a pawn, a stats actor) and return the factor your game scales on — a level curve,
 * a difficulty multiplier, prestige, anything. UScaledAmountReward reads it off the activation Instigator.
 *
 * This is the reference pattern for "a reward looks up game-specific state before granting": the value lives in YOUR
 * game's systems, so the reward asks for it through an interface rather than coupling to your classes. Implement it
 * directly, fork it, or cast to your own type instead — it's a shown pattern, not a mandate.
 */
UINTERFACE(MinimalAPI, Blueprintable)
class URewardScalingSource : public UInterface
{
	GENERATED_BODY()
};

class IRewardScalingSource
{
	GENERATED_BODY()

public:
	/** Multiplier applied to a scaled reward's base amount (1.0 = no change). Compute from level, difficulty, prestige… */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Reward")
	float GetRewardScale() const;
};