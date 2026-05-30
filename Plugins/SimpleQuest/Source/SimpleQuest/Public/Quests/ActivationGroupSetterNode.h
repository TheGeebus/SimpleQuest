// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "ActivationGroupSetterNode.generated.h"

/**
 * Runtime instance for the editor "Activation Group: Entry" node — the SETTER half of a group portal pair.
 * When activation arrives at this node's input, it publishes FQuestActivationGroupTriggeredEvent on the
 * GroupTag's SignalSubsystem channel — every UActivationGroupListenerNode subscribed to that channel receives
 * the signal. Forwards activation locally regardless.
 *
 * Editor naming follows graph-flow direction (signal entering the group portal); runtime naming follows
 * functional role (this class publishes the group's signal).
 */
UCLASS()
class SIMPLEQUEST_API UActivationGroupSetterNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag GroupTag;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};