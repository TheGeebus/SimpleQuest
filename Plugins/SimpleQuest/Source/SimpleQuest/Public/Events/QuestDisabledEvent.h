// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Events/QuestEventBase.h"

#include "QuestDisabledEvent.generated.h"

/**
 * Symmetric partner to FQuestEnabledEvent. Published when a previously-Enabled giver-gated quest's prereqs
 * transition from satisfied → unsatisfied - typically the NOT-prereq case where a leaf fact gets added that
 * inverts a NOT(...) clause's result, or a refcounted fact decrements through zero.
 *
 * Designers binding to OnQuestEnabled for "now ready" UI should also bind to OnQuestDisabled for the inverse
 * transition if their UI needs bidirectional sync (locked / unlocked indicator swap, dialogue tone revert,
 * etc.). Designers who only care about one direction can ignore the other event.
 *
 * Lifecycle: fires only after the quest is Activated and has previously fired Enabled at least once. A quest
 * that's Activated but hasn't yet satisfied its prereqs never fires Disabled - the "starting state" is just
 * "not enabled," which is communicated via the absence of FQuestEnabledEvent rather than a Disabled fire.
 *
 * Can fire multiple times per quest lifetime if prereqs oscillate (satisfied → unsatisfied → satisfied → unsatisfied ...).
 * Pairs symmetrically with FQuestEnabledEvent on each transition.
 */
USTRUCT(BlueprintType)
struct FQuestDisabledEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestDisabledEvent() = default;

	explicit FQuestDisabledEvent(const FGameplayTag InQuestTag)
		: FQuestEventBase(InQuestTag) {}

	FQuestDisabledEvent(const FGameplayTag InQuestTag, const FQuestEventContext& InContext)
		: FQuestEventBase(InQuestTag, InContext) {}
};