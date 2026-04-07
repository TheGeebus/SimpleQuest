#pragma once

#include "Events/QuestEventBase.h"

#include "QuestlineEndedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestlineEndedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestlineEndedEvent() = default;

	FQuestlineEndedEvent(const FGameplayTag InQuestTag, const bool bInDidSucceed)
		: FQuestEventBase(InQuestTag), bDidSucceed(bInDidSucceed) {}

	UPROPERTY(BlueprintReadWrite)
	bool bDidSucceed = false;
};
