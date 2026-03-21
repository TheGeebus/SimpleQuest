#pragma once

#include "Events/QuestEventBase.h"

#include "QuestStepStartedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestStepStartedEvent : public FQuestEventBase
{
	GENERATED_BODY()
	
	FQuestStepStartedEvent() = default;

	FQuestStepStartedEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, const int32 InStepID)
		: FQuestEventBase(InQuestID, InQuestClass), StepID(InStepID) {}

	UPROPERTY(BlueprintReadWrite)
	int32 StepID = -1;
};