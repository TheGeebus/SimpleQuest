// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/QuestNodeBase.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "RemoveFactNode.generated.h"

/**
 * Utility node that removes one or more WorldState facts when activated, then forwards activation. Lets a questline
 * graph publish state into the SimpleCore fact registry directly — without carrying its own quest-tag identity
 * or rich resolution record. Consumers anywhere in the project (UI, AI, audio, game-state systems) can subscribe
 * to those facts via FWorldStateFactRemovedEvent or query via UWorldStateSubsystem::HasFact without knowing
 * anything about quests.
 *
 * Compiled from UQuestlineNode_AddFact via FQuestlineGraphCompiler::CompileUtilityNodes.
 */
UCLASS()
class SIMPLEQUEST_API URemoveFactNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

protected:
	/** Facts to remove when this node activates. Each tag is asserted into WorldState with the configured BroadcastMode. */
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer Facts;

	/**
	 * Boundary-broadcast mode applied to every fact removed by this node. BoundaryOnly (default) fires
	 * FWorldStateFactRemovedEvent only on the 1→0 transition; Always fires on every call regardless of ref-count;
	 * Suppress never fires (silent assertion). Same semantic as UWorldStateSubsystem::RemoveFact's parameter.
	 */
	UPROPERTY(EditDefaultsOnly)
	EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};