// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Events/QuestEventBase.h"
#include "QuestTryStartEvent.generated.h"

/**
 * Published by a quest giver when the player requests to start a quest.
 * The QuestManagerSubsystem subscribes to this event and calls StartQuest if prerequisites are met.
 * Distinct from FQuestStartedEvent, which is published after the quest has actually started.
 */
USTRUCT(BlueprintType)
struct FTryQuestStartEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FTryQuestStartEvent() = default;

	explicit FTryQuestStartEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass)
		: FQuestEventBase(InQuestID, InQuestClass) {}
};
