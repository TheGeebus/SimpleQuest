// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestActivationGuardDecision.generated.h"


/**
 * The decision returned by FQuestActivationGuard::Evaluate / DecideFromInputs over an
 * activation request's runtime state. Each value names a distinct path through
 * UQuestManagerSubsystem::ActivateNodeByTag's gate region:
 *   - Refuse* values: caller logs and returns; no side effects.
 *   - ContainerReentry: caller logs the re-entry, then falls through to the post-gate
 *     side effects (Deactivated clear, cascade origin, IncomingOutcomeTag stamp,
 *     PendingEntryActivations append, Activate call). The Block check passed.
 *   - GiverGateFire: caller runs the giver-gate handler block (SetQuestPendingGiver +
 *     FQuestActivatedEvent / FQuestEnabledEvent publish + EnablementWatch register) and
 *     returns. Block is intentionally NOT pre-checked so giver-gated UI stays interactive
 *     while a quest is Blocked.
 *   - GiverGateSkipPathAware: caller logs the path-aware skip, then falls through to the
 *     post-gate side effects (Block was checked and passed).
 *   - Proceed: no diamond hit, no giver gate, not blocked — caller proceeds to the side
 *     effects directly.
 *
 * Decisions follow ActivateNodeByTag's gate sequence (diamond → giver → Block) so the
 * caller's switch reads top-to-bottom in flow order.
 */
UENUM(BlueprintType)
enum class EQuestActivationGuardDecision : uint8
{
    Proceed                        UMETA(DisplayName = "Proceed"),
    RefuseStepAlreadyLive          UMETA(DisplayName = "Refuse - Step Already Live"),
    RefuseStepAlreadyPendingGiver  UMETA(DisplayName = "Refuse - Step Already PendingGiver"),
    RefuseBlocked                  UMETA(DisplayName = "Refuse - Blocked"),
    ContainerReentry               UMETA(DisplayName = "Container Reentry"),
    GiverGateSkipPathAware         UMETA(DisplayName = "Giver Gate - Path-Aware Skip"),
    GiverGateFire                  UMETA(DisplayName = "Giver Gate - Fire"),
};

