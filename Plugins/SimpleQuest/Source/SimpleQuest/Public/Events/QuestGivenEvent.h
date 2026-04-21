// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
#include "Events/QuestEventBase.h"
#include "QuestGivenEvent.generated.h"

// Routing channel for give-quest request events. Pass as the Channel argument to PublishMessage.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestGiven)

USTRUCT(BlueprintType)
struct FQuestGivenEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestGivenEvent() = default;
	explicit FQuestGivenEvent(const FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
};
