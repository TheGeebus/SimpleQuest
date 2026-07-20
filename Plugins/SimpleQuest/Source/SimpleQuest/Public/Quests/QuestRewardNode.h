// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/Types/QuestRewardPreview.h"
#include "QuestRewardNode.generated.h"

class UQuestRewardBase;
struct FQuestRewardContext;
struct FQuestRewardActivationContext;

/**
 * Utility node that grants one or more rewards when activated, then forwards activation. Holds an Instanced array of
 * self-configuring UQuestRewardBase adapters; on activation it dispatches each (TryGrantReward), finalizes every queued
 * grant with the activation lineage, and publishes it as an FQuestRewardGrantedEvent on the grant's RewardType channel.
 *
 * Tagless + fire-when-reached (no Live / Started / Completed) — grant-once across save is free because restore never
 * re-reaches a fire-and-forward node. Compiled from UQuestlineNode_Reward (standalone) and synthesized from an Outcome
 * node's inline rewards, both via FQuestlineGraphCompiler. QuestContentGuid is the only durable identity handle.
 */
UCLASS()
class SIMPLEQUEST_API UQuestRewardNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

public:
	/**
	 * Preview aggregation — ask every reward this node holds what it WOULD grant, without granting. Iterates the
	 * Instanced Rewards and dispatches DescribeReward on each, concatenating the results. Pure: no delivery, no event,
	 * no forward. Backs the advertisement query (UQuestStateSubsystem::GetAdvertisedRewards).
	 */
	TArray<FQuestRewardPreview> DescribeRewards(AActor* Viewer) const;

	/**
	 * Source-agnostic advertised-rewards walk — the shared core of both the live query (manager: nodes from
	 * LoadedNodeInstances) and the cold query (asset: nodes from CompiledNodes). Given a completing node's manifest and
	 * the node map to resolve keys against, gathers the requested path's reward-node keys (plus the any-outcome bucket
	 * when merging), resolves each to a UQuestRewardNode, and aggregates DescribeReward. Pure: no grant, no event.
	 * PathIdentity NAME_None queries the any-outcome bucket; bIncludeAnyOutcome merges it into a named path.
	 */
	static TArray<FQuestRewardPreview> ResolveAdvertisedFromManifest(
		const TMap<FName, FQuestReachableRewards>& Manifest,
		const TMap<FName, TObjectPtr<UQuestNodeBase>>& NodeMap,
		FName PathIdentity,
		AActor* Viewer,
		bool bIncludeAnyOutcome);

	/**
	 * Grant a reward set — shared core of node grants (ActivateInternal) and questline-level grants (at resolution).
	 * Dispatches each reward's TryGrantReward against Incoming, finalizes every queued grant with the completion lineage
	 * (Instigator / Origin*, recipient defaulting to the Instigator), publishes each on its RewardType channel. Signals
	 * passed explicitly so a non-node caller (the manager) grants without a node's CachedGameInstance.
	 */
	static void GrantRewardSet(
		const TArray<TObjectPtr<UQuestRewardBase>>& Rewards,
		const FQuestRewardActivationContext& Incoming,
		USignalSubsystem* Signals);
	
protected:
	/** The rewards granted when this node activates, in order. Each computes + delivers independently. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UQuestRewardBase>> Rewards;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};