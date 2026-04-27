// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "UObject/WeakObjectPtr.h"

#include "QuestGiveBlockedEvent.generated.h"

class AActor;

/**
 * Published on the quest tag channel when a give attempt (FQuestGivenEvent → HandleGiveQuestEvent) is refused
 * by the manager because one or more activation blockers are present (unmet prereqs, Blocked state, etc.).
 *
 * Subscription model: the giver subscribes one-shot to this event immediately before publishing
 * FQuestGivenEvent and unsubscribes on receipt of either this event OR FQuestStartedEvent (the symmetric
 * "Yes" partner). Global subscribers (debug overlays, telemetry, party UI) can opt in to the channel directly
 * without coupling to the giver-side flow.
 *
 * Carries the structured FQuestActivationBlocker array so designer-side dialogue / UI can surface contextual
 * refusal text ("go talk to the innkeeper", "complete X first", "this quest is locked"). GiverActor identifies
 * which actor's component initiated the attempt — useful for telemetry, party UI attributing the response to
 * a specific NPC, and multi-giver scenarios.
 */
USTRUCT(BlueprintType)
struct FQuestGiveBlockedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestGiveBlockedEvent() = default;

	explicit FQuestGiveBlockedEvent(const FGameplayTag InQuestTag)
		: FQuestEventBase(InQuestTag) {}

	FQuestGiveBlockedEvent(const FGameplayTag InQuestTag, const TArray<FQuestActivationBlocker>& InBlockers, AActor* InGiverActor)
		: FQuestEventBase(InQuestTag)
		, Blockers(InBlockers)
		, GiverActor(InGiverActor) {}

	/**
	 * One entry per distinct blocker condition. The event only fires when at least one blocker is present, so
	 * this array is always non-empty when received.
	 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FQuestActivationBlocker> Blockers;

	/**
	 * The giver actor whose component called GiveQuestByTag. May be null if the event was synthesized from a
	 * non-giver path (rare; e.g., direct manager-side call without a triggering giver).
	 */
	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<AActor> GiverActor;
};