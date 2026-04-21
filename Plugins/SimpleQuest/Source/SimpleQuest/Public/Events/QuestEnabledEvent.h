#pragma once

#include "Events/QuestEventBase.h"

#include "QuestEnabledEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestEnabledEvent : public FQuestEventBase
{	
	GENERATED_BODY()
	
	FQuestEnabledEvent() = default;

	FQuestEnabledEvent(const FGameplayTag InQuestTag)
		: FQuestEventBase(InQuestTag) {}

	FQuestEnabledEvent(const FGameplayTag InQuestTag, const FQuestEventContext& InContext)
		: FQuestEventBase(InQuestTag, InContext) {}
};