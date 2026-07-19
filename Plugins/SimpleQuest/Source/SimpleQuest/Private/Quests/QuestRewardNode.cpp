// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestRewardNode.h"

#include "Rewards/QuestRewardBase.h"
#include "Events/QuestRewardGrantedEvent.h"
#include "Subsystems/SignalSubsystem.h"
#include "SimpleQuestLog.h"

TArray<FQuestRewardPreview> UQuestRewardNode::DescribeRewards(AActor* Viewer) const
{
	TArray<FQuestRewardPreview> Previews;
	for (const TObjectPtr<UQuestRewardBase>& Reward : Rewards)
	{
		if (Reward) Previews.Append(Reward->DispatchDescribeReward(Viewer));
	}
	return Previews;
}

TArray<FQuestRewardPreview> UQuestRewardNode::ResolveAdvertisedFromManifest(
	const TMap<FName, FQuestReachableRewards>& Manifest,
	const TMap<FName, TObjectPtr<UQuestNodeBase>>& NodeMap,
	FName PathIdentity,
	AActor* Viewer,
	bool bIncludeAnyOutcome)
{
	// The requested path's reward keys, plus the any-outcome (NAME_None) bucket when merging. AddUnique dedups a key
	// that sits in both buckets (a reward on Any Outcome AND the named path).
	TArray<FName> RewardKeys;
	if (const FQuestReachableRewards* PathBucket = Manifest.Find(PathIdentity))
	{
		RewardKeys = PathBucket->RewardNodeKeys;
	}
	if (!PathIdentity.IsNone() && bIncludeAnyOutcome)
	{
		if (const FQuestReachableRewards* AnyBucket = Manifest.Find(NAME_None))
		{
			for (const FName& Key : AnyBucket->RewardNodeKeys) RewardKeys.AddUnique(Key);
		}
	}

	TArray<FQuestRewardPreview> Previews;
	for (const FName& Key : RewardKeys)
	{
		if (const UQuestRewardNode* RewardNode = Cast<UQuestRewardNode>(NodeMap.FindRef(Key)))
		{
			Previews.Append(RewardNode->DescribeRewards(Viewer));
		}
	}
	return Previews;
}

void UQuestRewardNode::GrantRewardSet(const TArray<TObjectPtr<UQuestRewardBase>>& Rewards, const FQuestRewardActivationContext& Incoming, USignalSubsystem* Signals)
{
	if (!Signals)
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("GrantRewardSet: no SignalSubsystem — %d reward(s) not published."), Rewards.Num());
		return;
	}

	for (UQuestRewardBase* Reward : Rewards)
	{
		if (!Reward) continue;

		Reward->DispatchTryGrantReward(Incoming);
		for (FQuestRewardContext& Grant : Reward->TakePendingGrants())
		{
			if (!Grant.RewardType.IsValid())
			{
				UE_LOG(LogSimpleQuestActivation, Warning,
					TEXT("GrantRewardSet: a reward delivered a grant with no RewardType — dropped (nothing to route on)."));
				continue;
			}

			Grant.Instigator         = Incoming.Instigator;
			Grant.OriginTag          = Incoming.OriginTag;
			Grant.OriginChain        = Incoming.OriginChain;
			Grant.OriginatingEventID = Incoming.OriginatingEventID;
			if (!Grant.Recipient.IsValid()) Grant.Recipient = Incoming.Instigator;

			UE_LOG(LogSimpleQuestActivation, Log, TEXT("GrantRewardSet: granting '%s' (recipient: %s)"),
				*Grant.RewardType.ToString(), Grant.Recipient.IsValid() ? TEXT("targeted") : TEXT("broadcast / instigator"));

			Signals->PublishMessage(Grant.RewardType, FQuestRewardGrantedEvent(Grant));
		}
	}
}

void UQuestRewardNode::ActivateInternal(FGameplayTag InContextualTag)
{
	FQuestRewardActivationContext Incoming;
	static_cast<FQuestContextBase&>(Incoming) = PendingActivationContext.IncomingContext;
	Incoming.Provenance                       = PendingActivationContext.Provenance;
	Incoming.IncomingOutcomeTag               = PendingActivationContext.IncomingOutcomeTag;

	UGameInstance* GI = CachedGameInstance.Get();
	USignalSubsystem* Signals = GI ? GI->GetSubsystem<USignalSubsystem>() : nullptr;
	GrantRewardSet(Rewards, Incoming, Signals);

	ForwardActivation();
}
