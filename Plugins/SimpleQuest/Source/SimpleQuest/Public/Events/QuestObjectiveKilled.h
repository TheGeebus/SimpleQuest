# pragma once

#include "Events/QuestObjectiveTriggered.h"

#include "QuestObjectiveKilled.generated.h"

USTRUCT(BlueprintType)
struct FQuestObjectiveKilled : public FQuestObjectiveTriggered
{
	GENERATED_BODY()

	FQuestObjectiveKilled() = default;

	FQuestObjectiveKilled(AActor* InVictimActor, AActor* InKillerActor)
		: FQuestObjectiveTriggered(InVictimActor), KillerActor(InKillerActor) {}

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> KillerActor;
};