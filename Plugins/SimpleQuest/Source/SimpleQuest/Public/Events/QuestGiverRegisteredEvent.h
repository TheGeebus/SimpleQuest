// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
#include "Events/QuestEventBase.h"
#include "QuestGiverRegisteredEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestGiverRegistered)

USTRUCT(BlueprintType)
struct FQuestGiverRegisteredEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestGiverRegisteredEvent() = default;
	explicit FQuestGiverRegisteredEvent(const FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
};
