// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "QuestTriggerDeactivatedEvent.generated.h"

/**
 * Reason the trigger-side lifecycle wrapped. Completed and Interrupted publish automatically from the manager adjacent to
 * FQuestEndedEvent / FQuestDeactivatedEvent respectively; Manual publishes from UQuestObjective::PublishTriggerDeactivation
 * when an objective wants to signal "I'm done consuming fires on this trigger" without ending the step itself.
 */
UENUM(BlueprintType)
enum class EQuestTriggerEndReason : uint8
{
	/** Step completed normally with OutcomeTag. */
	Completed,

	/** Step was interrupted before completion (abandon, cascade-deactivate, blocked-with-deactivate). */
	Interrupted,

	/** Objective fired PublishTriggerDeactivation explicitly. Step lifecycle is unaffected; trigger-side audience only. */
	Manual,
};

/**
 * Published on the step's tag channel when the trigger-side of a step's lifecycle wraps: completion, interruption, or an
 * explicit signal from inside an objective via PublishTriggerDeactivation. Carries the last context that drove resolution so
 * the trigger actor's wrap-up logic can reference "who finished me off" without recomputing.
 *
 * Distinct from FQuestEndedEvent / FQuestDeactivatedEvent: those are the step's lifecycle transitions consumed by observers and
 * cascade routing; this event is the trigger-side wrap signal consumed by UQuestTriggerComponent's OnQuestTriggerDeactivated.
 * Manual publishes don't touch lifecycle state — they only notify the trigger audience.
 */
USTRUCT(BlueprintType)
struct FQuestTriggerDeactivatedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestTriggerDeactivatedEvent() = default;

	FQuestTriggerDeactivatedEvent(const FGameplayTag InQuestTag, EQuestTriggerEndReason InEndReason,
		const FGameplayTag InOutcomeTag, const FQuestObjectiveTriggerContext& InFinalContext)
		: FQuestEventBase(InQuestTag), EndReason(InEndReason), OutcomeTag(InOutcomeTag), FinalContext(InFinalContext) {}

	UPROPERTY(BlueprintReadOnly)
	EQuestTriggerEndReason EndReason = EQuestTriggerEndReason::Completed;

	/** Populated for Completed (and optionally Manual if the objective supplies an outcome); invalid for Interrupted. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag OutcomeTag;

	/** Last trigger context that drove resolution — from the Step's CompletionContext for Completed, from caller for Manual. */
	UPROPERTY(BlueprintReadOnly)
	FQuestObjectiveTriggerContext FinalContext;
};