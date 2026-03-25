#pragma once

#include "Events/QuestEventBase.h"

#include "QuestObjectiveTriggered.generated.h"

USTRUCT(BlueprintType)
struct FQuestObjectiveTriggered : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestObjectiveTriggered() = default;

	FQuestObjectiveTriggered(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, AActor* InTriggeredActor)
		: FQuestEventBase(InQuestID, InQuestClass), TriggeredActor(InTriggeredActor) {}
		
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> TriggeredActor;
};