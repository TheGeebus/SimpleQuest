#pragma once

#include "Events/QuestEventBase.h"

#include "QuestStepStartedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestStepStartedEvent : public FQuestEventBase
{
	GENERATED_BODY()
	
	FQuestStepStartedEvent() = default;

	FQuestStepStartedEvent(const FGameplayTag InQuestTag)
		: FQuestEventBase(InQuestTag) {}

};