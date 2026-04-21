#pragma once

#include "Events/QuestEventBase.h"

#include "QuestStartedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestStartedEvent : public FQuestEventBase
{	
	GENERATED_BODY()
	
	FQuestStartedEvent() = default;

	explicit FQuestStartedEvent(const FGameplayTag InQuestTag)
		: FQuestEventBase(InQuestTag) {}

	FQuestStartedEvent(const FGameplayTag InQuestTag, const FQuestEventContext& InContext)
		: FQuestEventBase(InQuestTag, InContext) {}
};