#pragma once

#include "Signals/SignalEventBase.h"
#include "QuestEventBase.generated.h"

class UQuest;

USTRUCT(BlueprintType)
struct FQuestEventBase : public FSignalEventBase
{
	GENERATED_BODY()

	FQuestEventBase() = default;

	FQuestEventBase(const FGameplayTag InQuestTag, const TSubclassOf<UQuest>& InQuestClass)
		: FSignalEventBase(InQuestTag.GetTagName()), QuestClass(InQuestClass), QuestTag(InQuestTag)
	{
		EventTag = InQuestTag;
	}

	UPROPERTY()
	TSubclassOf<UQuest> QuestClass;

	UPROPERTY()
	FGameplayTag QuestTag;
};