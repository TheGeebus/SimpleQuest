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
	
protected:
	/** The rewards granted when this node activates, in order. Each computes + delivers independently. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UQuestRewardBase>> Rewards;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;

private:
	/** Fill a grant's lineage from the activation context, default its Recipient to the Instigator, and publish it. */
	void FinalizeAndPublishGrant(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) const;
};