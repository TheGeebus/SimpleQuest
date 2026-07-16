// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Rewards/QuestRewardBase.h"
#include "GenericReward.generated.h"

/**
 * The no-subclass reward: a designer sets a RewardType and a raw payload struct in the details panel and it delivers
 * them as-is on activation — no C++, no Blueprint subclass. This is suitable for static rewards where no computation or
 * lookup is required before the reward event is delivered to the recipient — such as granting the player a story-related
 * item, notifying the HUD to activate a particular UI effect, or telling an external system like a cutscene manager to
 * start some scripted event.
 */
UCLASS(meta = (DisplayName = "Generic Reward"))
class SIMPLEQUEST_API UGenericReward : public UQuestRewardBase
{
	GENERATED_BODY()

protected:
	/** The reward kind emitted — the publish channel and the recipient's branch key (e.g. SimpleQuest.Reward.Currency.Gold). */
	UPROPERTY(EditAnywhere, meta = (Categories = "SimpleQuest.Reward"), Category = "Reward")
	FGameplayTag RewardType;

	/** The payload the recipient reads via CustomData.Get<FYourType>(). Set via the struct picker. */
	UPROPERTY(EditAnywhere, Category = "Reward")
	FInstancedStruct Payload;

	virtual void TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming) override;
	virtual TArray<FQuestRewardPreview> DescribeReward_Implementation(const FQuestRewardPreviewContext& Context) const override;
};