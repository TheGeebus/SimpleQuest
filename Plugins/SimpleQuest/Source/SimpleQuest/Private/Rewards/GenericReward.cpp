// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/GenericReward.h"

void UGenericReward::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	// Deliver the configured type and payload as-is. Invalid type = a misconfigured entry; grant nothing.
	if (RewardType.IsValid())
	{
		DeliverReward(RewardType, Payload);
	}
}

TArray<FQuestRewardPreview> UGenericReward::DescribeReward_Implementation(const FQuestRewardPreviewContext& Context) const
{
	if (!RewardType.IsValid()) return {};
	FQuestRewardPreview P;
	P.RewardType  = RewardType;
	P.PreviewData = Payload;
	return { P };
}

