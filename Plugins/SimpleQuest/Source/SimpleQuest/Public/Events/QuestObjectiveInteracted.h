#pragma once

#include "Events/QuestObjectiveTriggered.h"

#include "QuestObjectiveInteracted.generated.h"

USTRUCT(BlueprintType)
struct FQuestObjectiveInteracted : public FQuestObjectiveTriggered
{
	GENERATED_BODY()

	FQuestObjectiveInteracted() = default;

	FQuestObjectiveInteracted(AActor* InQuestingActor, AActor* InInteractingActor)
		: FQuestObjectiveTriggered(InQuestingActor, InInteractingActor), InteractingActor(InInteractingActor) {}

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> InteractingActor;
};