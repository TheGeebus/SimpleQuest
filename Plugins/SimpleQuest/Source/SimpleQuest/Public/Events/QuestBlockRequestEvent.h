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
 *
 * Set bAlsoDeactivate=true to publish FQuestDeactivateRequestEvent alongside the block. Default false preserves
 * the Block / Deactivate orthogonality — Block is the re-entry gate, Deactivate is the lifecycle interrupt.
 * Mirrors the graph-driven SetBlocked node's bAlsoDeactivateTargets toggle.
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
	FQuestBlockRequestEvent(FGameplayTag InQuestTag, EDeactivationSource InSource, const FQuestEventPayload& InContext, bool InAlsoDeactivate)
	: FQuestEventBase(InQuestTag), Source(InSource), Context(InContext), bAlsoDeactivate(InAlsoDeactivate) {}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::External;

	/** Optional attribution payload. Empty default; manager propagates non-empty Context into FQuestBlockedEvent.Payload. */
	UPROPERTY(BlueprintReadWrite)
	FQuestEventPayload Context;

	/**
	 * When true, the manager publishes FQuestDeactivateRequestEvent for the same quest alongside the block.
	 * Independent of block-side idempotency — if the quest is already blocked but not yet deactivated, the
	 * deactivation event still publishes. Default false preserves Block / Deactivate orthogonality.
	 */
	UPROPERTY(BlueprintReadWrite)
	bool bAlsoDeactivate = false;
};