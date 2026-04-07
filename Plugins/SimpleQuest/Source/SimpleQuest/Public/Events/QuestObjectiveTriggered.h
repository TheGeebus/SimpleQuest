#pragma once

#include "Events/QuestEventBase.h"

#include "QuestObjectiveTriggered.generated.h"

USTRUCT(BlueprintType)
struct FQuestObjectiveTriggered : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestObjectiveTriggered() = default;

	FQuestObjectiveTriggered(AActor* InTriggeredActor)
		: FQuestEventBase(FGameplayTag()), TriggeredActor(InTriggeredActor) {}
		
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> TriggeredActor;
};