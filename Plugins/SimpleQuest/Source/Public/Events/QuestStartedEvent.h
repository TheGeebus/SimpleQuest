#pragma once

#include "Events/QuestEventBase.h"

#include "QuestStartedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestStartedEvent : public FQuestEventBase
{	
	GENERATED_BODY()
	
	FQuestStartedEvent() = default;

	explicit FQuestStartedEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass)
		: FQuestEventBase(InQuestID, InQuestClass) {}
};