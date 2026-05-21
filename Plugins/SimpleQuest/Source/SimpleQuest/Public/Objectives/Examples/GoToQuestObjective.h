// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

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

public:
	UGoToQuestObjective();
	
protected:
	virtual void TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& InContext) override;
	virtual void OnObjectiveActivated_Implementation(const FQuestObjectiveActivationContext& Params) override;

private:
	UPROPERTY(EditDefaultsOnly, meta = (Categories = "SimpleQuest.Outcome", ObjectiveOutcome))
	FGameplayTag ReachedOutcomeTag;
};
