// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "AbandonQuestEvent.generated.h"

// Routing channel for abandon request events. Pass as the Channel argument to PublishMessage.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestAbandoned)

USTRUCT(BlueprintType)
struct FAbandonQuestEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FAbandonQuestEvent() = default;
	FAbandonQuestEvent(FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
};