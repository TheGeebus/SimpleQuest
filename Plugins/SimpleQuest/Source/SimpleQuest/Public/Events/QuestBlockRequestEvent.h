// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "QuestBlockRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestBlockRequest)

/**
 * Publish on Tag_Channel_QuestBlockRequest to ask the manager to mark a quest as Blocked. Manager mirrors the
 * USetBlockedNode logic — adds the Blocked WorldState fact and publishes a Deactivate request with Internal source
 * so any active node tears down cleanly. BP-friendly counterpart to graph-driven SetBlocked nodes.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestBlockRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestBlockRequestEvent() = default;
	explicit FQuestBlockRequestEvent(FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
};