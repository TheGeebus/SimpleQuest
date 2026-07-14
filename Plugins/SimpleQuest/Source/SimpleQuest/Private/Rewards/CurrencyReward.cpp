// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/CurrencyReward.h"
#include "Quests/Types/QuestRewardPayloads.h"
#include "NativeGameplayTags.h"
#include "StructUtils/InstancedStruct.h"
#include "SimpleQuestLog.h"

// The currency category root — registered so a Currency Reward's type picker has a namespace. Adopters define their
// specific currencies under it (SimpleQuest.Reward.Currency.Gold, .Gems, …); the framework ships no specific currency.
UE_DEFINE_GAMEPLAY_TAG(TAG_Reward_Currency, "SimpleQuest.Reward.Currency");

void UCurrencyReward::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	if (Amount <= 0) return;

	if (!RewardType.IsValid())
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("UCurrencyReward: no currency set (RewardType) — nothing granted. Pick a SimpleQuest.Reward.Currency.* tag."));
		return;
	}
	DeliverReward(RewardType, FInstancedStruct::Make<FQuestRewardAmount>(FQuestRewardAmount{ Amount }));
}