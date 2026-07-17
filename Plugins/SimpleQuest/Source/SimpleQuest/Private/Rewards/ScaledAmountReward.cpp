// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/ScaledAmountReward.h"

#include "GameFramework/Actor.h"
#include "Rewards/RewardScalingSource.h"
#include "Quests/Types/QuestRewardPayloads.h"
#include "StructUtils/InstancedStruct.h"
#include "SimpleQuestLog.h"

void UScaledAmountReward::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	if (!RewardType.IsValid() || BaseAmount <= 0) return;

	// Look up the scaling factor from the recipient. Reach game state through the Instigator actor — NOT this reward's
	// own GetWorld(), which is null: the reward instance is owned by the questline graph asset, not a world.
	//
	// This is the intended "do a lookup from another game system before sending" pattern — cast the instigator to whatever
	// interface (or class) your reward needs and read it.
	//
	// URewardScalingSource is a simple example interface that provides a float owned by another class, such as player
	// level or some other game-specific value. Returns 1.f by default, so no instigator or a non-implementing instigator
	// has no effect.
	float Scale = 1.f;
	if (AActor* Instigator = Incoming.Instigator.Get())
	{
		if (Instigator->Implements<URewardScalingSource>())
		{
			Scale = IRewardScalingSource::Execute_GetRewardScale(Instigator);
		}
	}

	const int32 Amount = FMath::RoundToInt(BaseAmount * Scale);
	if (Amount <= 0) return;

	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UScaledAmountReward: base %d x scale %.2f -> %d of '%s'."),
		BaseAmount,
		Scale,
		Amount,
		*RewardType.ToString());

	DeliverReward(RewardType, FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ Amount }));
}

TArray<FQuestRewardPreview> UScaledAmountReward::DescribeReward_Implementation(AActor* Viewer) const
{
	if (!RewardType.IsValid() || BaseAmount <= 0) return {};

	float Scale = 1.f;
	if (IsValid(Viewer) && Viewer->Implements<URewardScalingSource>())
	{
		Scale = IRewardScalingSource::Execute_GetRewardScale(Viewer);
	}

	FQuestRewardPreview P;
	P.RewardType  = RewardType;
	P.PreviewData = FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ FMath::RoundToInt(BaseAmount * Scale) });
	return { P };
}
