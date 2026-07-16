// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/XPReward.h"
#include "Quests/Types/QuestRewardPayloads.h"
#include "NativeGameplayTags.h"
#include "StructUtils/InstancedStruct.h"

// Native reward-type tag for experience. Defined here so it's registered before any CDO reads it (also auto-registers
// the SimpleQuest.Reward root, giving the generic + currency reward pickers a namespace).
UE_DEFINE_GAMEPLAY_TAG(TAG_Reward_Experience, "SimpleQuest.Reward.Experience");

void UXPReward::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	if (Amount <= 0) return;   // nothing to grant; declining is legal and never affects graph flow
	DeliverReward(TAG_Reward_Experience, FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ Amount }));
}

TArray<FQuestRewardPreview> UXPReward::DescribeReward_Implementation(const FQuestRewardPreviewContext& Context) const
{
	if (Amount <= 0) return {};
	FQuestRewardPreview P;
	P.RewardType  = TAG_Reward_Experience;
	P.PreviewData = FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ Amount });
	return { P };
}

