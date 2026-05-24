// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/QuestNodeBase.h"
#include "ClearFactNode.generated.h"

/**
 * Utility node that clears one or more WorldState facts when activated (regardless of ref-count), then forwards
 * activation. Use for hard resets where the ref-count semantic of Remove Fact would be wrong — e.g., "the door
 * is permanently unlocked now, drop the fact regardless of how many systems set it."
 *
 * Compiled from UQuestlineNode_ClearFact via FQuestlineGraphCompiler::CompileUtilityNodes.
 */
UCLASS()
class SIMPLEQUEST_API UClearFactNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

protected:
	/** Facts to clear when this node activates. Each tag is fully removed from WorldState regardless of ref-count. */
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer Facts;

	/**
	 * When true, the FactRemoved event is suppressed even on the present→absent transition. Default false matches
	 * UWorldStateSubsystem::ClearFact's default — clearing a present fact broadcasts the boundary event so
	 * subscribers can react to the state change. Use true for silent cleanup (PIE reset, save-load rehydration,
	 * etc. — adopter-side use rare).
	 */
	UPROPERTY(EditDefaultsOnly)
	bool bSuppressBroadcast = false;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};