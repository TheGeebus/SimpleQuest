// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestRewardNode.h"

#include "Rewards/QuestRewardBase.h"
#include "Events/QuestRewardGrantedEvent.h"
#include "Subsystems/SignalSubsystem.h"
#include "SimpleQuestLog.h"

TArray<FQuestRewardPreview> UQuestRewardNode::DescribeRewards(AActor* Viewer, FGameplayTag ResolvingQuestTag) const
{
	TArray<FQuestRewardPreview> Previews;
	for (const TObjectPtr<UQuestRewardBase>& Reward : Rewards)
	{
		if (Reward) Previews.Append(Reward->DispatchDescribeReward(Viewer, ResolvingQuestTag));
	}
	return Previews;
}

TArray<FQuestRewardPreview> UQuestRewardNode::ResolveAdvertisedFromManifest(
	const TMap<FName, FQuestReachableRewards>& Manifest,
	const TMap<FName, TObjectPtr<UQuestNodeBase>>& NodeMap,
	FName PathIdentity,
	AActor* Viewer,
	bool bIncludeAnyOutcome, FGameplayTag ResolvingQuestTag)
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
			Previews.Append(RewardNode->DescribeRewards(Viewer, ResolvingQuestTag));
		}
	}
	return Previews;
}

void UQuestRewardNode::GrantRewardSet(const TArray<TObjectPtr<UQuestRewardBase>>& Rewards, const FQuestRewardActivationContext& Incoming, USignalSubsystem* Signals)
{
	if (!Signals)
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("GrantRewardSet: no SignalSubsystem - %d reward(s) not published."), Rewards.Num());
		return;
	}

	for (UQuestRewardBase* Reward : Rewards)
	{
		if (!Reward) continue;

		Reward->DispatchTryGrantReward(Incoming);
		for (FQuestRewardContext& Grant : Reward->TakePendingGrants())
		{
			// Modifiers run BEFORE the validity check and before lineage is stamped. Before the check so one test
			// covers both what the reward produced and what a modifier turned it into; before the stamp so provenance
			// is written last and a modifier cannot corrupt it.
			if (!Reward->ApplyModifiers(Grant, Incoming)) continue;

			if (!Grant.RewardType.IsValid())
			{
				UE_LOG(LogSimpleQuestActivation, Warning,
					TEXT("GrantRewardSet: a reward or one of its modifiers produced a grant with no RewardType - dropped (nothing to route on)."));
				continue;
			}

			Grant.Instigator         = Incoming.Instigator;
			Grant.OriginTag          = Incoming.OriginTag;
			Grant.OriginChain        = Incoming.OriginChain;
			Grant.OriginatingEventID = Incoming.OriginatingEventID;
			Grant.RewardGuid         = Reward->RewardGuid;   // pairs with the advertisement's stamp; see FQuestRewardContext
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
	static_cast<FQuestContextBase&>(Incoming) = PendingActivationContext.IncomingParams;
	Incoming.Provenance                       = PendingActivationContext.Provenance;
	Incoming.IncomingOutcomeTag               = PendingActivationContext.IncomingOutcomeTag;
	// A reward node is tagless, so the nearest thing carrying a resolution history is whatever cascaded into it - which is
	// what OriginTag already holds. Reached from something that records no resolution, the count stays zero and nothing gates.
	Incoming.ResolvingQuestTag                = Incoming.OriginTag;

	UGameInstance* GI = CachedGameInstance.Get();
	USignalSubsystem* Signals = GI ? GI->GetSubsystem<USignalSubsystem>() : nullptr;
	GrantRewardSet(Rewards, Incoming, Signals);

	ForwardActivation();
}
