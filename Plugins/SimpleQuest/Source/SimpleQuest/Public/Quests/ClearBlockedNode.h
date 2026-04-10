// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "ClearBlockedNode.generated.h"

UCLASS()
class SIMPLEQUEST_API UClearBlockedNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;
	
protected:
	/** The quest node tag to unblock. */
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer TargetQuestTags;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;

public:
	FORCEINLINE const FGameplayTagContainer& GetTargetQuestTags() const { return TargetQuestTags; }
};
