#pragma once

#include "Events/QuestEventBase.h"

#include "QuestlineEndedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestlineEndedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestlineEndedEvent() = default;

	FQuestlineEndedEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, const bool bInDidSucceed)
		: FQuestEventBase(InQuestID, InQuestClass), bDidSucceed(bInDidSucceed) {}

	UPROPERTY(BlueprintReadWrite)
	bool bDidSucceed = false;
};
