#pragma once

#include "Events/QuestObjectiveTriggered.h"

#include "QuestObjectiveInteracted.generated.h"

USTRUCT(BlueprintType)
struct FQuestObjectiveInteracted : public FQuestObjectiveTriggered
{
	GENERATED_BODY()

	FQuestObjectiveInteracted() = default;

	FQuestObjectiveInteracted(AActor* InQuestingActor, AActor* InInteractingActor,
		const FInstancedStruct& InCustomData = FInstancedStruct())
		: FQuestObjectiveTriggered(InQuestingActor, InInteractingActor, InCustomData), InteractingActor(InInteractingActor) {}

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> InteractingActor;
};