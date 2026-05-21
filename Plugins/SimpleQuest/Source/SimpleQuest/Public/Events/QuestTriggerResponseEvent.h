// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "QuestTriggerResponseEvent.generated.h"

/**
 * Resolution categories for the objective's per-fire response to a trigger event. Progress / Completed publish automatically from
 * ReportProgress / CompleteObjectiveWithOutcome; Refused publishes manually from UQuestObjective::RefuseTrigger when the objective
 * sees a fire but declines to act on it (game-logic reason). Adopters branching on Resolution in a single OnQuestTriggerResponded
 * handler can drive distinct UI / audio per case without binding to several delegates.
 */
UENUM(BlueprintType)
enum class EQuestTriggerResolution : uint8
{
	/** Objective consumed the fire and advanced internal progress (counter increment, partial state, etc.). */
	Progress,

	/** Objective consumed the fire and resolved with OutcomeTag. Terminal — no further Response events on this fire. */
	Completed,

	/** Objective received the fire and declined to act on it. RefusalReason carries the designer-defined "why not" tag. */
	Refused,
};

/**
 * Published on the step's tag channel when an objective produces a per-fire response to a trigger event. Distinct from the
 * step-level lifecycle events (FQuestProgressEvent / FQuestEndedEvent) — those fire on state transitions regardless of which
 * specific fire drove them; this event ties each publish to the originating SendTriggerEvent fire via TriggerContext.
 *
 * Audience: UQuestTriggerComponent's OnQuestTriggerResponded delegate. Subscribers filter on TriggerContext.TriggeredActor ==
 * GetOwner() to scope to their own fires (mirrors the Giver pattern's GiverActor filter on OnQuestGiveBlocked).
 *
 * Refused is the only path objectives must publish explicitly (via RefuseTrigger); Progress and Completed publish automatically
 * alongside the existing lifecycle events.
 */
USTRUCT(BlueprintType)
struct FQuestTriggerResponseEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestTriggerResponseEvent() = default;

	FQuestTriggerResponseEvent(const FGameplayTag InQuestTag, EQuestTriggerResolution InResolution,
		const FQuestObjectiveTriggerContext& InTriggerContext)
		: FQuestEventBase(InQuestTag), Resolution(InResolution), TriggerContext(InTriggerContext) {}

	FQuestTriggerResponseEvent(const FGameplayTag InQuestTag, EQuestTriggerResolution InResolution, const FGameplayTag InOutcomeTag,
		const FGameplayTag InRefusalReason, const FQuestObjectiveTriggerContext& InTriggerContext)
		: FQuestEventBase(InQuestTag), Resolution(InResolution), OutcomeTag(InOutcomeTag), RefusalReason(InRefusalReason),
		  TriggerContext(InTriggerContext) {}

	UPROPERTY(BlueprintReadOnly)
	EQuestTriggerResolution Resolution = EQuestTriggerResolution::Progress;

	/** Populated when Resolution == Completed; invalid tag otherwise. Matches the OutcomeTag passed to CompleteObjectiveWithOutcome. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag OutcomeTag;

	/** Populated when Resolution == Refused; designer-defined "why not" tag passed to RefuseTrigger. Invalid for non-Refused. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag RefusalReason;

	/** Echo of the originating SendTriggerEvent fire — TriggeredActor / Instigator / CustomData / counts. Subscribers filter here. */
	UPROPERTY(BlueprintReadOnly)
	FQuestObjectiveTriggerContext TriggerContext;
};