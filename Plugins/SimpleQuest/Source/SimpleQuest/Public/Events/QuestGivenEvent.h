// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "NativeGameplayTags.h"
#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "QuestGivenEvent.generated.h"

// Routing channel for give-quest request events. Pass as the Channel argument to PublishMessage.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestGiven)

USTRUCT(BlueprintType)
struct FQuestGivenEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestGivenEvent() = default;
	explicit FQuestGivenEvent(const FGameplayTag InQuestTag) : FQuestEventBase(InQuestTag) {}
	FQuestGivenEvent(const FGameplayTag InQuestTag, const FQuestObjectiveActivationContext& InParams)
		: FQuestEventBase(InQuestTag), Params(InParams) {}

	/** Giver-authored activation payload. Stamped onto the target step's PendingActivationContext by
		UQuestManagerSubsystem::HandleGiveQuestEvent and merged with the step's authored defaults in
		UQuestStep::ActivateInternal (additive — sets union, counts sum, CustomData + Instigator overwrite). */
	UPROPERTY(BlueprintReadWrite)
	FQuestObjectiveActivationContext Params;
};