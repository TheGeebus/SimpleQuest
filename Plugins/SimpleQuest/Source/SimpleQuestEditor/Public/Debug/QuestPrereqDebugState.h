// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestPrereqDebugState.generated.h"

/**
 * Live runtime state of a prerequisite-expression leaf or combinator during PIE, driving the Prereq Examiner's background
 * coloring. Leaves use the full 5-state range (including NotStarted / InProgress nuance for mid-flight source nodes);
 * combinators collapse to a binary Unsatisfied / Satisfied (matching the runtime's boolean evaluation semantics) + Unknown
 * when not in PIE.
 */
UENUM()
enum class EPrereqDebugState : uint8
{
	/** Not in PIE, channel not active, or the leaf/combinator's tags couldn't be resolved. Renders neutral — no tint. */
	Unknown,

	/** Leaf-only: source node has no state facts yet (hasn't activated). Renders grey. */
	NotStarted,

	/** Leaf-only: source node is Active or PendingGiver, outcome not yet resolved. Renders amber. */
	InProgress,

	/** Evaluates false right now. Leaf: source completed/deactivated without producing the fact this leaf checks.
		Combinator: its boolean eval is currently false. Renders red. */
	Unsatisfied,

	/** Evaluates true right now. Leaf: checked fact is present in WorldState. Combinator: boolean eval is true. Renders green. */
	Satisfied
};