#pragma once

#include "QuestEventBase.generated.h"


USTRUCT(BlueprintType)
struct FQuestEventBase
{
	GENERATED_BODY()

	FQuestEventBase() = default;

	explicit FQuestEventBase(const FGameplayTag InQuestTag)
		: QuestTag(InQuestTag)
	{}
	
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag QuestTag;
	
	FGameplayTag GetQuestTag() const { return QuestTag; }

};
