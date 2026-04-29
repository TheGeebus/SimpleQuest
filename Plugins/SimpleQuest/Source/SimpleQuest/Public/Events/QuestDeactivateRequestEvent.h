// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "QuestDeactivatedEvent.h"
#include "QuestDeactivateRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestDeactivateRequest)

/**
 * Publish on Tag_Channel_QuestDeactivateRequest to ask the manager to deactivate a quest. Source distinguishes
 * graph-driven deactivations (SetBlocked node, designer-wired Deactivate input) from external/game-code calls
 * (USimpleQuestBlueprintLibrary::DeactivateQuest, save-system rehydration). Manager passes Source through to
 * FQuestDeactivatedEvent so subscribers can branch UI/telemetry on origin.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestDeactivateRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()
	
	FQuestDeactivateRequestEvent() = default;
	FQuestDeactivateRequestEvent(FGameplayTag InQuestTag, EDeactivationSource InSource)
		: FQuestEventBase(InQuestTag), Source(InSource) {}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::Internal;
};

