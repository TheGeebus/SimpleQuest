// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/QuestRewardBase.h"

#include "SimpleQuestLog.h"

void UQuestRewardBase::DispatchTryGrantReward(const FQuestRewardActivationContext& Incoming)
{
	// Route through the UFunction thunk so Blueprint overrides of the BlueprintNativeEvent fire.
	TryGrantReward(Incoming);
}

void UQuestRewardBase::TryGrantReward_Implementation(const FQuestRewardActivationContext& Incoming)
{
	// Pure adapter: the base grants nothing. Concrete subclasses override this; UGenericReward delivers its configured
	// RewardType + Payload. A Blueprint reward that doesn't implement TryGrantReward simply grants nothing.
}

void UQuestRewardBase::DeliverReward(FGameplayTag InRewardType, const FInstancedStruct& InPayload, AActor* Recipient)
{
	FQuestRewardContext Grant;
	Grant.RewardType = InRewardType;
	Grant.CustomData = InPayload;
	Grant.Recipient  = Recipient;   // may be null: the reward node defaults it to the activation Instigator

	UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestRewardBase::DeliverReward queued grant '%s' (explicit recipient: %s)"),
		*InRewardType.ToString(), Recipient ? TEXT("yes") : TEXT("no — defaults to instigator"));

	PendingGrants.Add(MoveTemp(Grant));
}