#pragma once

#include "Events/QuestEventBase.h"

#include "QuestStepCompletedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestStepCompletedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestStepCompletedEvent() = default;

	FQuestStepCompletedEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, const int32 InStepID, const bool bInDidSucceed)
		: FQuestEventBase(InQuestID, InQuestClass), StepID(InStepID), bDidSucceed(bInDidSucceed) {}

	UPROPERTY(BlueprintReadWrite)
	int32 StepID = -1;
	UPROPERTY(BlueprintReadWrite)
	bool bDidSucceed = false;
};