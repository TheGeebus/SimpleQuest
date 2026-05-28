// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/PrerequisiteExpression.h"

#include "QuestActivatedEvent.generated.h"

/**
 * Published the moment a quest enters scope — the universal "first notice" entry-point signal, intended as the
 * natural UI binding point for any handler that wants to react when a quest becomes observable. Always fires on
 * first activation-wire arrival, regardless of prereq state.
 *
 * Three activation paths fire this event:
 *   - Giver-gated paths fire it at the giver-gate decision (PendingGiver state reached). PrereqStatus carries
 *     the actually-evaluated leaf snapshot — designers branch on bSatisfied for locked-vs-ready UI states.
 *   - Non-gated content-node paths fire it in HandleOnNodeStarted alongside FQuestStartedEvent (the cascade
 *     reached this node without a giver waypoint, so scope-entry and Live-transition collapse into a single
 *     call). PrereqStatus carries the default (bIsAlways=true, bSatisfied=true) — by definition the cascade only
 *     reached this node after upstream prereq deferral cleared.
 *   - Asset-level activation publishes it on the questline's own tag channel at ActivateQuestlineGraph time,
 *     alongside FQuestStartedEvent. Symmetric with PublishGraphResolutions's close-out publish (§4.36).
 *     PrereqStatus carries the default; assets don't have a prereq concept (gating lives on individual content
 *     nodes inside the graph).
 *
 * Adopters who specifically care about the giver-gating distinction branch on the carrying instance's
 * bWasGiverGated flag or check PrereqStatus.bIsAlways. Wrapper re-entries while already Live suppress this event
 * (no-op re-activation isn't a transition).
 *
 * Late-subscriber catch-up: UQuestLifecycleObserver::RunCatchUp reconstructs this event for subscribers that
 * bind after the live publish fired. Reconstructs from EITHER the PendingGiver fact (giver-gated path entered
 * PendingGiver scope and is still there) OR the Live fact (non-gated path or asset-level activation, where being
 * Live implies having transitioned through Activated). Catch-up runs on next tick via FTimerManager — adopters
 * binding mid-mission reliably see the Activated signal even if their subscription completes after the questline
 * already started.
 *
 * Distinct from FQuestEnabledEvent (which fires only when the quest is genuinely accept-ready, i.e., Activated
 * AND prereqs satisfy). Binding to OnQuestActivated gets "first notice" semantics; binding to OnQuestEnabled
 * gets "now actually startable" semantics. Pick whichever fits the present need; bind to both for the
 * "show locked indicator on activation, swap to ready indicator on enablement" flow.
 */
USTRUCT(BlueprintType)
struct FQuestActivatedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestActivatedEvent() = default;

	explicit FQuestActivatedEvent(const FGameplayTag InQuestTag)
		: FQuestEventBase(InQuestTag) {}

	FQuestActivatedEvent(const FGameplayTag InQuestTag, const FQuestEventPayload& InContext, const FQuestPrereqStatus& InPrereqStatus)
		: FQuestEventBase(InQuestTag, InContext)
		, PrereqStatus(InPrereqStatus) {}

	/**
	 * Snapshot of the quest's prereq evaluation at the moment Activation fires. Designers branch on
	 * PrereqStatus.bSatisfied to decide whether to surface a "ready" or "locked" UI immediately, and read
	 * PrereqStatus.Leaves for contextual hints about which prereqs the player still needs to satisfy. 
	 */
	UPROPERTY(BlueprintReadOnly)
	FQuestPrereqStatus PrereqStatus;
};