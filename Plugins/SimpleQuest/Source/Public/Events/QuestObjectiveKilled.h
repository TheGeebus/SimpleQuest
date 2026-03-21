# pragma once

#include "Events/QuestObjectiveTriggered.h"

#include "QuestObjectiveKilled.generated.h"

USTRUCT(BlueprintType)
struct FQuestObjectiveKilled : public FQuestObjectiveTriggered
{
	GENERATED_BODY()

	FQuestObjectiveKilled() = default;

	FQuestObjectiveKilled(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, AActor* InVictimActor, AActor* InKillerActor)
		: FQuestObjectiveTriggered(InQuestID, InQuestClass, InVictimActor), KillerActor(InKillerActor) {}

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> KillerActor;
};