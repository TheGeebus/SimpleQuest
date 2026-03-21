#pragma once

#include "CoreMinimal.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestStepCompletedEvent.h"
#include "Events/QuestStepStartedEvent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "QuestEventHelpers.generated.h"

#define DECLARE_EVENT_HELPER(TypeName) \
UFUNCTION(BlueprintPure, Category = "Quest Events") \
static UScriptStruct* Get##TypeName##EventType() { return F##TypeName##Event::StaticStruct(); }

UCLASS()
class UQuestEventHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	DECLARE_EVENT_HELPER(QuestEnabled)
	DECLARE_EVENT_HELPER(QuestStarted)
	DECLARE_EVENT_HELPER(QuestStepStarted)
	DECLARE_EVENT_HELPER(QuestStepCompleted)
	DECLARE_EVENT_HELPER(QuestEnded)
};
