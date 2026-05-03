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
	/** The quest node tags to block. */
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer TargetQuestTags;

	/**
	 * When true, ActivateInternal also publishes FQuestDeactivateRequestEvent for each target tag in addition to
	 * writing the Blocked WorldState fact. Default false — Block is purely a re-entry gate by default; designers
	 * opt in to in-flight interruption explicitly. Mirrors the editor node's bAlsoDeactivateTargets UPROPERTY.
	 */
	UPROPERTY(EditDefaultsOnly)
	bool bAlsoDeactivateTargets = false;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};
