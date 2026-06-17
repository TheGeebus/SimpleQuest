// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "QuestProgressRefusedEvent.generated.h"

/**
 * Published on a step's tag channel when an interaction (typically a trigger fire) attempts to progress the
 * step but is refused — most commonly because a prerequisite isn't satisfied, but also covers Blocked-state
 * and similar mid-Live refusal paths. Fires when the step is structurally reachable (manager has registered
 * it) but cannot currently make forward progress. Silent on steps the runtime hasn't reached (Idle, Completed,
 * UnknownQuest) and silent on Live + unblocked steps where progress is accepted normally.
 *
 * Rounds out the refusal-event family at the run phase: FQuestActivationFailedEvent covers the offer-phase
 * refusal, FQuestGiveBlockedEvent covers the give-phase refusal, FQuestProgressRefusedEvent covers the
 * run-phase progress refusal. All three publish on the quest's tag channel and are observable through the
 * standard UQuestObserverComponent pipeline (per-event delegate OR the OnAnyQuestEvent catch-all).
 *
 * Subscription model: bind per-tag at the refused step's tag, or at any registered ancestor for broader
 * scope. Carries the structured FQuestActivationBlocker array so designer-side UI can surface contextual
 * refusal text ("complete X first", "this gate stays closed until Y"). TriggerContext echoes the originating
 * trigger fire so subscribers can attribute the refusal to the specific interaction; filter on
 * TriggerContext.TriggeredActor == GetOwner() for own-fire scoping when receiving via an inherited delegate
 * on a trigger component.
 */
USTRUCT(BlueprintType)
struct FQuestProgressRefusedEvent : public FQuestEventBase
{
    GENERATED_BODY()

    FQuestProgressRefusedEvent() = default;

    FQuestProgressRefusedEvent(const FGameplayTag InQuestTag, const TArray<FQuestActivationBlocker>& InBlockers,
        const FQuestObjectiveTriggerContext& InTriggerContext)
        : FQuestEventBase(InQuestTag), Blockers(InBlockers), TriggerContext(InTriggerContext) {}

    /**
     * One entry per distinct blocker condition. The event only fires when at least one structural blocker
     * (Blocked or PrereqUnmet) is present, so this array is always non-empty when received.
     */
    UPROPERTY(BlueprintReadOnly)
    TArray<FQuestActivationBlocker> Blockers;

    /** Echo of the originating trigger fire — TriggeredActor / Instigator / CustomData / counts. */
    UPROPERTY(BlueprintReadOnly)
    FQuestObjectiveTriggerContext TriggerContext;
};