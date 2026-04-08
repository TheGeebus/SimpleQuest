// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "QuestEventBase.h"
#include "AbandonQuestEvent.generated.h"


USTRUCT(BlueprintType)
struct FAbandonQuestEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FAbandonQuestEvent() = default;
	FAbandonQuestEvent(FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
};