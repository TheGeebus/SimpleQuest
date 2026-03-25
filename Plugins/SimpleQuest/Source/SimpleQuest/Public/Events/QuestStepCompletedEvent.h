#pragma once

#include "Events/QuestEventBase.h"

#include "QuestStepCompletedEvent.generated.h"

class UQuestReward;

USTRUCT(BlueprintType)
struct FQuestStepCompletedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestStepCompletedEvent() = default;

	FQuestStepCompletedEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, const int32 InStepID, const bool bInDidSucceed, const bool bInEndedQuest, UQuestReward* InReward)
		: FQuestEventBase(InQuestID, InQuestClass), StepID(InStepID), bDidSucceed(bInDidSucceed), bEndedQuest(bInEndedQuest), Reward(InReward) {}

	UPROPERTY(BlueprintReadWrite)
	int32 StepID = -1;
	UPROPERTY(BlueprintReadWrite)
	bool bDidSucceed = false;
	UPROPERTY(BlueprintReadWrite)
	bool bEndedQuest = false;
	UPROPERTY(BlueprintReadWrite)
	UQuestReward* Reward = nullptr;
};