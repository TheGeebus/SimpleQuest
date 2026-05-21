// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "QuestDeactivatedEvent.h"
#include "Quests/Types/QuestEventPayload.h"
#include "QuestDeactivateRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestDeactivateRequest)

/**
 * Publish on Tag_Channel_QuestDeactivateRequest to ask the manager to deactivate a quest. Source distinguishes
 * graph-driven deactivations (SetBlocked node, designer-wired Deactivate input) from external/game-code calls
 * (USimpleQuestBlueprintLibrary::DeactivateQuest, save-system rehydration). Manager passes Source through to
 * FQuestDeactivatedEvent so subscribers can branch UI/telemetry on origin. Context carries optional attribution
 * (Instigator, CustomData, OriginTag, OriginChain) propagated into FQuestDeactivatedEvent.Payload.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestDeactivateRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()
	
	FQuestDeactivateRequestEvent() = default;
	FQuestDeactivateRequestEvent(FGameplayTag InQuestTag, EDeactivationSource InSource)
		: FQuestEventBase(InQuestTag), Source(InSource) {}
	FQuestDeactivateRequestEvent(FGameplayTag InQuestTag, EDeactivationSource InSource, const FQuestEventPayload& InContext)
		: FQuestEventBase(InQuestTag), Source(InSource), Context(InContext) {}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::Internal;

	/** Optional attribution payload. Empty default; manager propagates non-empty Context into FQuestDeactivatedEvent.Payload. */
	UPROPERTY(BlueprintReadWrite)
	FQuestEventPayload Context;
};