// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "GroupSignalGetterNode.generated.h"

struct FWorldStateFactAddedEvent;

UCLASS()
class SIMPLEQUEST_API UGroupSignalGetterNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	/**
	 * WorldState fact tag this getter listens for. Must match the GroupSignalTag on the corresponding setter. Output connects
	 * to any input type (Activate, Deactivate, or Prerequisite) — the getter is agnostic of downstream intent.
	 */
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag GroupSignalTag;
	
	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
	virtual void DeactivateInternal(FGameplayTag InContextualTag) override;

private:
	FDelegateHandle SignalSubscriptionHandle;

	void OnGroupSignalFired(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);

};
