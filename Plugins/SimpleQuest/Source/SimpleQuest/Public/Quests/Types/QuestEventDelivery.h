// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestEventDelivery.generated.h"

/**
 * How a quest event reached the subscriber: as a real-time transition, or reconstructed after the fact during a
 * catch-up pass. Rides on FQuestEventPayload so any observer can branch on it — most usefully to preempt visible
 * transition behavior. An NPC that patrols on Started and settles on Completed should, on a CatchUp delivery, jump
 * straight to the settled state instead of acting out a transition that already happened.
 *
 * CatchUp covers every reconstruction path — a late-registering observer in normal play, a post-load restore, and
 * (later) a multiplayer join-in-progress client — because the subscriber's response is identical for all three: this
 * is history being replayed, not a live change. If a consumer ever needs to distinguish WHY the catch-up ran, that's
 * a separate axis to add then, not a narrowing of this one.
 */
UENUM(BlueprintType)
enum class EQuestEventDelivery : uint8
{
	/** A real-time transition happening now. Play the full behavior. */
	Live,

	/** Reconstructed during a catch-up pass (late registration / save restore / join-in-progress). Collapse to the end state. */
	CatchUp,
};