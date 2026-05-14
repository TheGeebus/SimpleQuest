// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Events/QuestEventBase.h"
#include "Events/QuestDeactivatedEvent.h"
#include "QuestUnblockedEvent.generated.h"


/**
 * Fired when a quest's Blocked state fact transitions from present to absent — the "block-clear successful"
 * event corresponding to a ClearBlocked utility node activation or a Tag_Channel_QuestClearBlockRequest
 * publish. Symmetric partner to FQuestBlockedEvent. Idempotent at the publisher: only fires when the fact
 * actually transitions (clear-on-already-unblocked is skipped at the gate, no event emitted).
 *
 * Source classification mirrors FQuestBlockedEvent (Internal for graph-driven ClearBlocked nodes, External
 * for code-driven publishes). Reuses EDeactivationSource for the same Internal/External axis.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestUnblockedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestUnblockedEvent() = default;

	FQuestUnblockedEvent(const FGameplayTag InQuestTag, const EDeactivationSource InSource)
		: FQuestEventBase(InQuestTag), Source(InSource) {}

	FQuestUnblockedEvent(const FGameplayTag InQuestTag, const EDeactivationSource InSource, const FQuestEventPayload& InContext)
		: FQuestEventBase(InQuestTag, InContext), Source(InSource) {}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::Internal;
};