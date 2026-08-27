// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_Reward.generated.h"

class UQuestRewardBase;
class URewardSetDataAsset;

/**
 * Editor graph node that grants one or more rewards when activated, then forwards. Holds an Instanced array of
 * self-configuring UQuestRewardBase adapters, edited inline in the Details panel. Compiles to UQuestRewardNode via
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
	 * Rewards granted when this node activates, in order. Each entry is a configured UQuestRewardBase instance — pick a
	 * class (C++ or Blueprint subclass, or the base) and set its fields inline. Multiple rewards fire together.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Reward")
	TArray<TObjectPtr<UQuestRewardBase>> Rewards;

	/**
	 * Shared reward compositions this node also grants. Compilation flattens each set's contents into this node, so the
	 * runtime sees one flat list and nothing here reaches it.
	 *
	 * ORDER: referenced sets first, in the order listed, then the inline Rewards above. Stated rather than emergent,
	 * because grant sequence is meaning.
	 *
	 * SOFT on purpose - the compiler resolves it in the editor and the runtime never needs the asset, so a hard
	 * reference would drag it into the cook for nothing. Same reasoning as a loot reward's table.
	 */
	UPROPERTY(EditAnywhere, Category = "Reward")
	TArray<TSoftObjectPtr<URewardSetDataAsset>> RewardSets;
	
	/**
	 * Persistent expanded/collapsed state for the "Rewards" class list on the node widget. Transient (view state,
	 * not authored data) — mirrors content nodes' bGiversExpanded.
	 */
	UPROPERTY(Transient)
	bool bRewardsExpanded = false;
};