// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestRewardNode.h"

#include "Rewards/QuestReward.h"
#include "Events/QuestRewardGrantedEvent.h"
#include "Subsystems/SignalSubsystem.h"
#include "SimpleQuestLog.h"

void UQuestRewardNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility node, no Live / Started / Completed. Grant-when-reached: dispatch each reward,
	// publish its deliveries on the reward-type channel, then forward. The manager populated PendingActivationContext
	// before activation (lineage + provenance + incoming outcome); slice out the reward-relevant fields (drop objective Config).
	FQuestRewardActivationContext Incoming;
	static_cast<FQuestContextBase&>(Incoming)	= PendingActivationContext.IncomingContext;   // copy lineage only
	Incoming.Provenance							= PendingActivationContext.Provenance;
	Incoming.IncomingOutcomeTag					= PendingActivationContext.IncomingOutcomeTag;

	for (UQuestReward* Reward : Rewards)
	{
		if (!Reward) continue;

		Reward->DispatchTryGrantReward(Incoming);
		for (FQuestRewardContext& Grant : Reward->TakePendingGrants())
		{
			FinalizeAndPublishGrant(Grant, Incoming);
		}
	}

	ForwardActivation();
}

void UQuestRewardNode::FinalizeAndPublishGrant(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) const
{
	if (!Grant.RewardType.IsValid())
	{
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("UQuestRewardNode: a reward delivered a grant with no RewardType — dropped (nothing to route on)."));
		return;
	}

	// Framework-owned lineage the reward didn't supply. Default the recipient to whoever caused the activation
	// (the common "reward the player who finished it" case); invalid Recipient = broadcast to all type-subscribers.
	Grant.Instigator			= Incoming.Instigator;
	Grant.OriginTag				= Incoming.OriginTag;
	Grant.OriginChain			= Incoming.OriginChain;
	Grant.OriginatingEventID	= Incoming.OriginatingEventID;
	if (!Grant.Recipient.IsValid())
	{
		Grant.Recipient = Incoming.Instigator;
	}

	UGameInstance* GI = CachedGameInstance.Get();
	USignalSubsystem* Signals = GI ? GI->GetSubsystem<USignalSubsystem>() : nullptr;
	if (!Signals)
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("UQuestRewardNode: no SignalSubsystem — grant '%s' not published."),
			*Grant.RewardType.ToString());
		return;
	}

	UE_LOG(LogSimpleQuestActivation, Log, TEXT("UQuestRewardNode: granting '%s' (recipient: %s)"),
		*Grant.RewardType.ToString(),
		Grant.Recipient.IsValid() ? TEXT("targeted") : TEXT("broadcast / instigator"));

	Signals->PublishMessage(Grant.RewardType, FQuestRewardGrantedEvent(Grant));
}