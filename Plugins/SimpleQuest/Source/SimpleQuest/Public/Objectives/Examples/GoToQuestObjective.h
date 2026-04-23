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

public:
	UGoToQuestObjective();
	
protected:
	virtual void TryCompleteObjective_Implementation(const FQuestObjectiveContext& InContext) override;
	virtual void OnObjectiveActivated_Implementation(const FQuestObjectiveActivationParams& Params) override;

private:
	UPROPERTY(EditDefaultsOnly, meta = (Categories = "Quest.Outcome", ObjectiveOutcome))
	FGameplayTag ReachedOutcomeTag;
};
