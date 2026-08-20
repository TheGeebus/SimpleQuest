// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeDebugState.generated.h"

/**
 * Where a quest node currently sits in its lifecycle during PIE, as resolved from WorldState facts by
 * FQuestPIEDebugChannel. Values listed in presentation-priority order — the channel picks the highest-priority state the
 * node holds (PendingGiver > Live > Completed > Deactivated), because current activity outranks history: the Completed
 * fact is append-only and a container keeps Live across loop iterations, so a node running again asserts both.
 *
 * Why a node cannot proceed is a SEPARATE, orthogonal dimension — a node can be gated at any point in its lifecycle.
 * See FQuestPIEDebugChannel::QueryNodeGating, which reports that in the runtime's own blocker vocabulary.
 */
UENUM()
enum class EQuestNodeDebugState : uint8
{
	/** Not running in PIE, no runtime instance for this editor node, or no state facts set. */
	Unknown,

	/** Runtime instance exists but carries no state fact relevant to the debug overlay. Rare — distinct from Unknown only
		when the node was instantiated by ActivateQuestlineGraph but hasn't yet reached Live. */
	Deactivated,

	/** QuestState.<Tag>.Completed fact is set. */
	Completed,

	/** QuestState.<Tag>.Live fact is set. */
	Live,

	/** QuestState.<Tag>.PendingGiver fact is set — waiting on giver approval before activation. */
	PendingGiver
};

