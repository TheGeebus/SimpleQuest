// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "QuestTriggerBlockedEvent.generated.h"

/**
 * Published on a watched step's tag channel by UQuestTriggerComponent::SendTriggerEvent when the step is structurally reachable
 * (PendingGiver-or-similar — manager has registered it) but cannot currently make forward progress: the step's Blocked fact is
 * set, or its prereqs are unmet. Silent on steps the runtime hasn't reached (Idle, Completed, UnknownQuest) and silent on Live
 * steps (those route via FQuestTriggerFiredEvent → objective → FQuestTriggerResponseEvent).
 *
 * Mirrors the Giver-side FQuestGiveBlockedEvent shape — same FQuestActivationBlocker[] payload — so adopters who already handle
 * give-time blocker dialogues can reuse the same translator for trigger-time blocker UI.
 *
 * Audience: UQuestTriggerComponent's OnQuestTriggerBlocked delegate. Subscribers filter on TriggerContext.TriggeredActor ==
 * GetOwner() for own-fire scoping.
 */
USTRUCT(BlueprintType)
struct FQuestTriggerBlockedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestTriggerBlockedEvent() = default;

	FQuestTriggerBlockedEvent(const FGameplayTag InQuestTag, const TArray<FQuestActivationBlocker>& InBlockers,
		const FQuestObjectiveTriggerContext& InTriggerContext)
		: FQuestEventBase(InQuestTag), Blockers(InBlockers), TriggerContext(InTriggerContext) {}

	/**
	 * One entry per distinct blocker condition. The event only fires when at least one structural blocker (Blocked or
	 * PrereqUnmet) is present, so this array is always non-empty when received.
	 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FQuestActivationBlocker> Blockers;

	/** Echo of the originating SendTriggerEvent fire — TriggeredActor / Instigator / CustomData / counts. Subscribers filter here. */
	UPROPERTY(BlueprintReadOnly)
	FQuestObjectiveTriggerContext TriggerContext;
};