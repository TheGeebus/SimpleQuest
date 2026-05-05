// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "QuestDeactivatedEvent.h"
#include "QuestClearBlockRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestClearBlockRequest)

/**
 * Publish on Tag_Channel_QuestClearBlockRequest to ask the manager to clear a quest's Blocked state. BP-friendly
 * counterpart to graph-driven ClearBlocked nodes. Manager removes the Blocked WorldState fact and publishes
 * FQuestUnblockedEvent on the quest's tag channel, threading Source through so subscribers can branch on origin.
 * Idempotent: clear-on-already-unblocked requests are skipped without firing the event.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestClearBlockRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestClearBlockRequestEvent() = default;
	explicit FQuestClearBlockRequestEvent(FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
	FQuestClearBlockRequestEvent(FGameplayTag InQuestTag, EDeactivationSource InSource)
		: FQuestEventBase(InQuestTag), Source(InSource) {}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::External;
};