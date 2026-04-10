// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "SetBlockedNode.generated.h"

UCLASS()
class SIMPLEQUEST_API USetBlockedNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	/** The quest node tag to block. */
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer TargetQuestTags;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};
