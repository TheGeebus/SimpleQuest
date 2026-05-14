// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Events/QuestEventBase.h"
#include "Events/QuestDeactivatedEvent.h"
#include "QuestBlockedEvent.generated.h"


/**
 * Fired when a quest's Blocked state fact transitions from absent to present — the "block successful"
 * event corresponding to a SetBlocked utility node activation or a Tag_Channel_QuestBlockRequest publish.
 * Idempotent at the publisher: only fires when the fact actually transitions (already-blocked re-applications
 * are skipped at the gate, no event emitted).
 *
 * Source carries the origin classification: Internal for graph-driven SetBlocked nodes, External for
 * code-driven publishes (USimpleQuestBlueprintLibrary::SetQuestBlocked, save/load rehydration). Reuses
 * EDeactivationSource since the Internal/External axis is generic across lifecycle operations.
 *
 * Block failure (request refused) doesn't fire this event — the absence of the success event IS the
 * "block failed" signal. No payload is needed for failure; subscribers wanting positive failure notification
 * should query state explicitly after the request.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestBlockedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestBlockedEvent() = default;

	FQuestBlockedEvent(const FGameplayTag InQuestTag, const EDeactivationSource InSource)
		: FQuestEventBase(InQuestTag), Source(InSource) {}

	FQuestBlockedEvent(const FGameplayTag InQuestTag, const EDeactivationSource InSource, const FQuestEventPayload& InContext)
		: FQuestEventBase(InQuestTag, InContext), Source(InSource) {}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::Internal;
};