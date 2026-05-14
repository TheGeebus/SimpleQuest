// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Events/QuestEventBase.h"
#include "QuestProgressEvent.generated.h"

/**
 * Published on the step's tag channel each time an objective reports progress without completing. Carries the same
 * FQuestEventPayload as FQuestEndedEvent — NodeInfo for identification, CompletionContext for current progress state.
 */
USTRUCT(BlueprintType)
struct FQuestProgressEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestProgressEvent() = default;

	FQuestProgressEvent(const FGameplayTag InQuestTag, const FQuestEventPayload& InContext)
		: FQuestEventBase(InQuestTag, InContext) {}
};