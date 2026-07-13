// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_Reward.generated.h"

class UQuestReward;

/**
 * Editor graph node that grants one or more rewards when activated, then forwards. Holds an Instanced array of
 * self-configuring UQuestReward adapters, edited inline in the Details panel. Compiles to UQuestRewardNode via
 * FQuestlineGraphCompiler::CompileUtilityNodes — a tagless, fire-when-reached effect node (Enter → grant → Forward).
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_Reward : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "RewardTitle", "Grant Rewards");
	}

	/**
	 * Rewards granted when this node activates, in order. Each entry is a configured UQuestReward instance — pick a
	 * class (C++ or Blueprint subclass, or the base) and set its fields inline. Multiple rewards fire together.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Reward")
	TArray<TObjectPtr<UQuestReward>> Rewards;
};