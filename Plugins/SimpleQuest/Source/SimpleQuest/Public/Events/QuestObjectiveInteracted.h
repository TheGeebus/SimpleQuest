#pragma once

#include "Events/QuestObjectiveTriggered.h"

#include "QuestObjectiveInteracted.generated.h"

USTRUCT(BlueprintType)
struct FQuestObjectiveInteracted : public FQuestObjectiveTriggered
{
	GENERATED_BODY()

	FQuestObjectiveInteracted() = default;

	FQuestObjectiveInteracted(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, AActor* InQuestingActor, AActor* InInteractingActor)
		: FQuestObjectiveTriggered(InQuestID, InQuestClass, InQuestingActor), InteractingActor(InInteractingActor) {}

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> InteractingActor;
};