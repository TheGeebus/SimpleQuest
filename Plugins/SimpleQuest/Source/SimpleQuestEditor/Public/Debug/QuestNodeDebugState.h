// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeDebugState.generated.h"

/**
 * Runtime state of a quest node during PIE, as resolved from WorldState facts by FQuestPIEDebugChannel. Values listed in
 * presentation-priority order — the channel picks the highest-priority state the node currently holds
 * (Blocked > PendingGiver > Live > Completed > Deactivated) so the overlay surfaces the most-urgent-to-see condition.
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
	PendingGiver,

	/** QuestState.<Tag>.Blocked fact is set — highest visual priority. */
	Blocked
};