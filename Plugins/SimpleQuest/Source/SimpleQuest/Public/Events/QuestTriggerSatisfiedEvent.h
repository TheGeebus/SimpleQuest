// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "QuestEventBase.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "QuestTriggerSatisfiedEvent.generated.h"

/**
 * Published on the step's tag channel when an Objective signals that a specific Trigger Component's actor has been
 * ticked off a multi-target satisfaction list. Distinct from FQuestTriggerDeactivatedEvent (which is the step-side
 * lifecycle wrap signal applying to all watching components): this event names ONE specific actor whose contribution
 * has been consumed, and Trigger Components filter by SatisfiedActor == GetOwner() to react only on own-actor matches.
 *
 * Carries the originating context (so adopters can read the trigger fire that drove the satisfaction) and the
 * framework-stamped OriginatingTriggerComponent for own-fire filtering symmetry with the existing trigger response /
 * blocked / deactivated events.
 *
 * Primary consumer: UQuestTriggerComponent's OnQuestTriggerSatisfied delegate — adopters bind to disable trigger
 * visuals / collision / interaction prompts on the satisfied actor while the step continues for other targets.
 * Useful for "interact with all N actors" objectives where each actor needs to visually clear once consumed but
 * the step shouldn't complete until every target has fired.
 */
USTRUCT(BlueprintType)
struct FQuestTriggerSatisfiedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestTriggerSatisfiedEvent() = default;

	FQuestTriggerSatisfiedEvent(const FGameplayTag InQuestTag, AActor* InSatisfiedActor, const FQuestObjectiveTriggerContext& InContext)
		: FQuestEventBase(InQuestTag), SatisfiedActor(InSatisfiedActor), Context(InContext) {}

	/** The specific actor whose trigger contribution was consumed. Trigger Components filter by this == GetOwner(). */
	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<AActor> SatisfiedActor;

	/** Originating trigger context — the fire that drove the satisfaction. Carries OriginatingTriggerComponent + Instigator + CustomData. */
	UPROPERTY(BlueprintReadOnly)
	FQuestObjectiveTriggerContext Context;
};