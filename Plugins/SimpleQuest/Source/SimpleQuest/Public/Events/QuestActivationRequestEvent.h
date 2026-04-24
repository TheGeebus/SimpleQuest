// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "QuestActivationRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestActivationRequest)

/**
 * External entry point for programmatically activating a quest with runtime-supplied params. The manager subscribes to
 * Tag_Channel_QuestActivationRequest at Initialize; game code (procedural generators, dialogue systems, save/load
 * rehydration, test harnesses) publishes to this channel to drive activation without touching the manager's public API.
 *
 * Params ride through to the activating step's UQuestObjective::OnObjectiveActivated hook, where subclasses can pull
 * game-specific data from CustomData. Named fields (TargetActors, TargetClasses, NumElementsRequired) merge additively
 * with the Step's authored defaults — sets are unioned, counts are summed. See UQuestStep::ActivateInternal.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestActivationRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestActivationRequestEvent() = default;
	FQuestActivationRequestEvent(FGameplayTag InQuestTag, const FQuestObjectiveActivationParams& InParams)
		: FQuestEventBase(InQuestTag), Params(InParams) {}

	UPROPERTY(BlueprintReadWrite)
	FQuestObjectiveActivationParams Params;
};

