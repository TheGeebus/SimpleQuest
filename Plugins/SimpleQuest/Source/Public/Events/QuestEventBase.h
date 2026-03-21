#pragma once

#include "SignalEventBase.h"

#include "QuestEventBase.generated.h"

class UQuest;

USTRUCT(BlueprintType)
struct FQuestEventBase : public FSignalEventBase
{
	GENERATED_BODY()

	FQuestEventBase() = default;

	FQuestEventBase(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass)
		: FSignalEventBase(InQuestID), QuestClass(InQuestClass) {}
	
	UPROPERTY()
	TSubclassOf<UQuest> QuestClass;	
};