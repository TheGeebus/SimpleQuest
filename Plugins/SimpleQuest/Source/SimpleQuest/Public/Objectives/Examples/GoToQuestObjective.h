// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Objectives/QuestObjective.h"
#include "GoToQuestObjective.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Outcome_GoTo_Reached)

/**
 * 
 */
UCLASS()
class SIMPLEQUEST_API UGoToQuestObjective : public UQuestObjective
{
	GENERATED_BODY()

protected:

	virtual void TryCompleteObjective_Implementation(UObject* InTargetObject) override;
	virtual void SetObjectiveTarget_Implementation(const TSet<TSoftObjectPtr<AActor>>& InTargetActors, UClass* InTargetClass = nullptr, int32 NumElementsRequired = 0, bool bUseCounter = false) override;
};
