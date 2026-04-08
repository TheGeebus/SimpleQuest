#pragma once

#include "Events/QuestEventBase.h"

#include "QuestEndedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestEndedEvent : public FQuestEventBase
{	
	GENERATED_BODY()

	FQuestEndedEvent() = default;

	FQuestEndedEvent(const FGameplayTag InQuestTag, const FGameplayTag InOutcomeTag)
		: FQuestEventBase(InQuestTag), OutcomeTag(InOutcomeTag) {}

	UPROPERTY(BlueprintReadWrite)
	FGameplayTag OutcomeTag;
};