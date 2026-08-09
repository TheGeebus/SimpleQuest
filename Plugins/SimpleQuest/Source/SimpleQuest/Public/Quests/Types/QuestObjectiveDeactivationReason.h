// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestObjectiveDeactivationReason.generated.h"

/**
 * Why an Objective is being torn down. Read inside OnObjectiveDeactivated via GetDeactivationReason() so an override
 * can branch - release a reservation on interruption but not on success, say - which the parameterless hook could not
 * previously express.
 *
 * Deliberately BINARY. The Step reaches deactivation through one virtual (UQuestNodeBase::DeactivateInternal), so
 * abandon, blocked and cascade-deactivated arrive indistinguishable from each other; reporting them separately would
 * mean threading a reason through that virtual and breaking every adopter override of it. An objective that needs the
 * finer distinction can read its own step's state during the call, which is still live.
 */
UENUM(BlueprintType)
enum class EQuestObjectiveDeactivationReason : uint8
{
	/** Not inside a deactivation call. Reading the reason at any other time always yields this. */
	Unspecified,

	/** The objective completed and reported an outcome. Its completion context is still readable during the call. */
	Completed,

	/** Torn down WITHOUT completing - the step was abandoned, blocked, or cascade-deactivated by an upstream node. */
	Interrupted
};

