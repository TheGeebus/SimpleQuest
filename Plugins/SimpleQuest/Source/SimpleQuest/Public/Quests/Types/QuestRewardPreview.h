// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "QuestRewardPreview.generated.h"

class AActor;

/**
 * Why an advertised reward would not currently be granted. A preview is never suppressed for being unavailable - it
 * comes back marked, so a UI can render "50 XP - requires Guild membership" rather than showing nothing and leaving the
 * player to guess the reward exists.
 */
USTRUCT(BlueprintType)
struct FQuestRewardBlocker
{
	GENERATED_BODY()

	/** The kind of block, under SimpleQuest.RewardBlocker. Branch on this for presentation; read Description for prose. */
	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	FGameplayTag BlockerType;

	/** Renderable explanation - "Already collected", "Requires Guild membership", "Available from run 3". */
	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	FText Description;
};

/**
 * One display line describing what a reward WOULD grant, for "do this task, get this reward" UI - produced by
 * UQuestRewardBase::DescribeReward without granting anything. The framework prescribes only RewardType (the reward's kind,
 * the same channel the grant publishes on, for grouping / icons); everything the UI renders lives in PreviewData, a
 * designer-defined struct read via PreviewData.Get<T>() (keyed by RewardType by convention). The grant's RewardType +
 * CustomData contract, on the preview side.
 */
USTRUCT(BlueprintType)
struct FQuestRewardPreview
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	FGameplayTag RewardType;

	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	FInstancedStruct PreviewData;

	/**
	 * The completion this was resolved from - the tag you queried on the node channel, the questline asset's identity
	 * on the questline channel. Stamped by the framework after DescribeReward returns, the same way a GRANT is stamped
	 * with its lineage after modifiers run; a reward never sets it, and could not, since it does not know what asked.
	 *
	 * It exists because both channels arrive merged in one list, and without it a designer looking at an unexpected
	 * entry cannot tell whether it came from a node in the graph or from the questline's own completion rewards. A
	 * grant could answer that; a preview could not.
	 *
	 * IT DOES NOT NAME THE AUTHORING SITE, and cannot: reward nodes are tagless (QuestContentGuid is their only durable
	 * handle), so no gameplay tag exists that would point at the Grant Rewards node an entry came from. "Jump to the
	 * reward that produced this line" needs a different field, not a better value in this one.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	FGameplayTag SourceTag;

	/**
	 * Identity of the reward that produced this line, copied from UQuestRewardBase::RewardGuid. Stamped by the
	 * framework alongside SourceTag; a reward never sets it.
	 *
	 * It exists so a UI can re-query without losing track of what it is showing. Advertised values are computed live
	 * for the viewer, so a journal refreshes constantly, and without an identity two rewards with matching numbers are
	 * indistinguishable from one reward recomputed - which forces a full widget rebuild and makes "this number just
	 * went up" impossible to detect.
	 *
	 * PER REWARD, NOT PER LINE: DescribeReward may return several previews, and they all carry the same GUID. It
	 * identifies what produced a line, not the line itself.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	FGuid RewardGuid;

	/**
	 * What currently stands between this reward and being granted. EMPTY means it would be granted right now.
	 *
	 * *** AN ADVERTISEMENT NEVER HIDES A REWARD FOR BEING UNAVAILABLE. *** Whether to show a blocked reward greyed, to
	 * filter it out, or to render its reason is a presentation decision, and the framework does not get to make it on
	 * a UI's behalf. More than one modifier can block the same reward, so this is a list rather than a verdict.
	 *
	 * These describe the PRESENT. A reward blocked by a condition that a later completion satisfies stops being blocked
	 * when it does - re-query and the entry comes back clean.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	TArray<FQuestRewardBlocker> Blockers;
};

/**
 * Map-legal wrapper for a list of previews - lets advertised rewards be returned keyed by outcome in a TMap (a bare
 * TArray can't be a TMap value in a BlueprintType/UFUNCTION signature).
 */
USTRUCT(BlueprintType)
struct FQuestRewardPreviewList
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	TArray<FQuestRewardPreview> Previews;
};

