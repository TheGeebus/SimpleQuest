// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "ActivationGroupListenerNode.generated.h"

struct FQuestActivationGroupTriggeredEvent;

/**
 * Runtime instance for the editor "Activation Group: Exit" node — the LISTENER half of a group portal pair.
 * Subscribes at instance lifetime (via OnRegisteredWithManager) to the GroupTag signal channel, listening for
 * FQuestActivationGroupTriggeredEvent published by every UActivationGroupSetterNode sharing the same group tag.
 * On signal received, stamps the payload's ForwardParams + OriginChain onto own PendingActivationContext and
 * calls ForwardActivation() to fire BoundaryCompletionsOnForward + activate NextNodesOnForward downstream.
 *
 * Subscription persists for the full instance lifetime regardless of containing wrapper's Live state — the
 * always-armed semantic supports cross-graph signaling and listeners-inside-inactive-wrappers cleanly.
 *
 * Editor naming follows graph-flow direction (signal exiting the group portal into local graph); runtime
 * naming follows functional role (this class subscribes to the group's signal).
 */
UCLASS()
class SIMPLEQUEST_API UActivationGroupListenerNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag GroupTag;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
	virtual void OnRegisteredWithManager() override;
	virtual void ResetTransientState() override;
	virtual void BeginDestroy() override;

private:
	FDelegateHandle SignalSubscriptionHandle;

	void OnGroupSignalReceived(FGameplayTag Channel, const FQuestActivationGroupTriggeredEvent& Event);
};