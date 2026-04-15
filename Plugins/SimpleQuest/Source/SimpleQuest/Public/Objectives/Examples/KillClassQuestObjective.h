// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Objectives/CountingQuestObjective.h"
#include "KillClassQuestObjective.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Outcome_KillClass_Killed)

/**
 * 
 */
UCLASS()
class SIMPLEQUEST_API UKillClassQuestObjective : public UCountingQuestObjective
{
	GENERATED_BODY()

public:
	virtual void TryCompleteObjective_Implementation(const FQuestObjectiveContext& InContext) override;
	
private:
	UPROPERTY(EditDefaultsOnly, meta = (Categories = "Quest.Outcome", ObjectiveOutcome))
	FGameplayTag TargetKilledOutcomeTag;

};
