// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "QuestDeactivatedEvent.h"
#include "Quests/Types/QuestEventPayload.h"
#include "QuestBlockRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestBlockRequest)

/**
 * Publish on Tag_Channel_QuestBlockRequest to ask the manager to mark a quest as Blocked. BP-friendly counterpart
 * to graph-driven SetBlocked nodes. Manager adds the Blocked WorldState fact and publishes FQuestBlockedEvent on
 * the quest's tag channel, threading Source through so subscribers can branch on origin (Internal = graph-driven,
 * External = BP/external publish). Idempotent: already-blocked requests are skipped without firing the event.
 * Context carries optional attribution (Instigator, CustomData, OriginTag, OriginChain) propagated into
 * FQuestBlockedEvent.Payload when the block is applied.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestBlockRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestBlockRequestEvent() = default;
	explicit FQuestBlockRequestEvent(FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
	FQuestBlockRequestEvent(FGameplayTag InQuestTag, EDeactivationSource InSource)
		: FQuestEventBase(InQuestTag), Source(InSource) {}
	FQuestBlockRequestEvent(FGameplayTag InQuestTag, EDeactivationSource InSource, const FQuestEventPayload& InContext)
		: FQuestEventBase(InQuestTag), Source(InSource), Context(InContext) {}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::External;

	/** Optional attribution payload. Empty default; manager propagates non-empty Context into FQuestBlockedEvent.Payload. */
	UPROPERTY(BlueprintReadWrite)
	FQuestEventPayload Context;
};