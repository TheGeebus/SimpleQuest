// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "QuestClearBlockRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestClearBlockRequest)

/**
 * Publish on Tag_Channel_QuestClearBlockRequest to ask the manager to remove the Blocked fact from a quest.
 * Manager mirrors the UClearBlockedNode logic: removes the Blocked WorldState fact only; Deactivated remains
 * until the target's Activate input fires (re-entry is the natural clear point for Deactivated). BP-friendly
 * counterpart to graph-driven ClearBlocked nodes.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestClearBlockRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestClearBlockRequestEvent() = default;
	explicit FQuestClearBlockRequestEvent(FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
};