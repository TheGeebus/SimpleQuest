#pragma once

#include "Events/QuestEventBase.h"

#include "QuestEnabledEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestEnabledEvent : public FQuestEventBase
{	
	GENERATED_BODY()
	
	FQuestEnabledEvent() = default;

	FQuestEnabledEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, const bool bNewIsActivated)
		: FQuestEventBase(InQuestID, InQuestClass), bIsActivated(bNewIsActivated) {}

	UPROPERTY()
	bool bIsActivated = false;
};