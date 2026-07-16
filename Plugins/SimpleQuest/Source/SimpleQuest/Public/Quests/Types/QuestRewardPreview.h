// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "QuestRewardPreview.generated.h"

class AActor;

/**
 * One display line describing what a reward WOULD grant, for "do this task, get this reward" UI — produced by
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
};

/**
 * Query-time input to DescribeReward. Viewer is the actor the preview is computed for (the player whose journal is open);
 * null = a generic, context-free preview. Static rewards ignore it; dynamic rewards (level-scaled) read it for a live value.
 */
USTRUCT(BlueprintType)
struct FQuestRewardPreviewContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Reward")
	TWeakObjectPtr<AActor> Viewer;
};

