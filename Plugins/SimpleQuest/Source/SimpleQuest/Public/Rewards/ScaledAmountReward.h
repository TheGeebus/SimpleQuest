// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Rewards/QuestRewardBase.h"
#include "ScaledAmountReward.generated.h"

/**
 * DEPRECATED - fuses a SOURCE with a TRANSFORM, which is why "scaled loot" would need a whole second class today.
 * Replace with an Amount Reward carrying the same type and base value, plus a Scale By Recipient modifier on it; the
 * pair behaves identically and the modifier composes with every other source. Removed in 0.9.
 *
 * HideDropdown keeps it out of the class picker so no new one can be authored, while existing ones keep working. The
 * questline compiler warns once per node still using one - a clean compile is the all-clear.
 */
UCLASS(HideDropdown, meta = (DisplayName = "Scaled Amount Reward"))
class SIMPLEQUEST_API UScaledAmountReward : public UQuestRewardBase
{
	GENERATED_BODY()

#if WITH_EDITOR
public:
	virtual FString DescribeDeprecation() const override;
#endif

protected:
	/** What to grant (Experience, Currency.Gold, …). */
	UPROPERTY(EditAnywhere, meta = (Categories = "SimpleQuest.Reward"), Category = "Reward")
	FGameplayTag RewardType;

	/** Amount at scale 1.0. The delivered amount is this times the recipient's GetRewardScale(). */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Reward")
	int32 BaseAmount = 0;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
	virtual TArray<FQuestRewardPreview> DescribeReward_Implementation(AActor* Viewer) const override;
};