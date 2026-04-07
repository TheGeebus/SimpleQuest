#pragma once

#include "Signals/SignalEventBase.h"
#include "QuestEventBase.generated.h"


USTRUCT(BlueprintType)
struct FQuestEventBase : public FSignalEventBase
{
	GENERATED_BODY()

	FQuestEventBase() = default;

	FQuestEventBase(const FGameplayTag InQuestTag)
		: FSignalEventBase(InQuestTag.GetTagName(), InQuestTag)
	{}

	FGameplayTag GetQuestTag() const { return EventTags.IsEmpty() ? FGameplayTag() : EventTags.GetByIndex(0); }

};