#pragma once

#include "QuestPrerequisiteCheckFailed.h"

#include "QuestStepPrereqCheckFailed.generated.h"

class UQuest;

USTRUCT(BlueprintType)
struct FQuestStepPrereqCheckFailed : public FQuestPrerequisiteCheckFailed
{
	GENERATED_BODY()
	
	FQuestStepPrereqCheckFailed() = default;

	FQuestStepPrereqCheckFailed(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, const int32 InQuestStepID)
		: FQuestPrerequisiteCheckFailed(InQuestID, InQuestClass), QuestStepID(InQuestStepID) {}

	UPROPERTY(BlueprintReadWrite)
	int32 QuestStepID = -1;	
};