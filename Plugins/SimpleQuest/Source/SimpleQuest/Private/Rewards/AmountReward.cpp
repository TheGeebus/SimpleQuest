// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/AmountReward.h"

#include "Quests/Types/QuestRewardPayloads.h"
#include "StructUtils/InstancedStruct.h"

void UAmountReward::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	if (!RewardType.IsValid() || Amount <= 0) return;

	DeliverReward(RewardType, FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ Amount }));
}

TArray<FQuestRewardPreview> UAmountReward::DescribeReward_Implementation(AActor* Viewer) const
{
	if (!RewardType.IsValid() || Amount <= 0) return {};

	// The authored amount, unmodified - DispatchDescribeReward runs this reward's modifiers over what comes back, so
	// applying them here as well would apply them twice.
	FQuestRewardPreview P;
	P.RewardType  = RewardType;
	P.PreviewData = FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ Amount });
	return { P };
}

