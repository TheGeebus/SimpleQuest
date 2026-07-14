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