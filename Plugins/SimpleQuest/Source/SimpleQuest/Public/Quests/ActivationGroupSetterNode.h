// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "ActivationGroupSetterNode.generated.h"

/**
 * Runtime instance for the editor "Activation Group: Entry" node — the SETTER half of a group portal pair.
 * When activation arrives at this node's input, it publishes the GroupTag (currently as a UWorldStateSubsystem
 * fact; switches to a transient FQuestActivationGroupTriggeredEvent on the GroupTag signal channel in a later
 * migration step). Forwards activation locally regardless.
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