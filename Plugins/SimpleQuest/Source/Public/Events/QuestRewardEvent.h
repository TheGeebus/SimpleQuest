#pragma once

#include "Events/QuestEventBase.h"

#include "QuestRewardEvent.generated.h"

class UQuestReward;

USTRUCT(BlueprintType)
struct FQuestRewardEvent : public FQuestEventBase
{
	GENERATED_BODY()
	
	FQuestRewardEvent() = default;

	FQuestRewardEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, UQuestReward* InRewardObject)
		: FQuestEventBase(InQuestID, InQuestClass), RewardObject(InRewardObject) {}
	
	UPROPERTY(BlueprintReadWrite)
	UQuestReward* RewardObject = nullptr;
};
