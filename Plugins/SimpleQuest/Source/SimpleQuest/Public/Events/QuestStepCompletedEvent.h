#pragma once

#include "Events/QuestEventBase.h"

#include "QuestStepCompletedEvent.generated.h"

class UQuestReward;

USTRUCT(BlueprintType)
struct FQuestStepCompletedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestStepCompletedEvent() = default;

	FQuestStepCompletedEvent(const FGameplayTag InQuestTag, const bool bInDidSucceed, const bool bInEndedQuest, UQuestReward* InReward)
		: FQuestEventBase(InQuestTag), bDidSucceed(bInDidSucceed), bEndedQuest(bInEndedQuest), Reward(InReward) {}

	UPROPERTY(BlueprintReadWrite)
	bool bDidSucceed = false;
	UPROPERTY(BlueprintReadWrite)
	bool bEndedQuest = false;
	UPROPERTY(BlueprintReadWrite)
	UQuestReward* Reward = nullptr;
};