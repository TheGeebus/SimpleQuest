// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "GroupSignalSetterNode.generated.h"

UCLASS()
class SIMPLEQUEST_API UGroupSignalSetterNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	/**
	 * WorldState fact written when this setter fires. Matching GroupSignalGetterNodes subscribe to this tag. "First signal wins":
	 * the 0-to-1 boundary transition is what triggers waiting getters.
	 */
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag GroupSignalTag;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
	
};
