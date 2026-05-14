// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestActivationGuardDecision.h"


class UQuestNodeBase;
class UWorldStateSubsystem;


/**
 * Pure boolean inputs to the activation guard's decision logic. Built by FQuestActivation-
 * Guard::Evaluate from runtime state (Instance kind, WorldState facts, registered-giver
 * set membership, optional path-aware reachability) and consumed by DecideFromInputs.
 *
 * Splitting inputs from decision keeps the policy pure-input-to-enum: tests synthesize
 * FQuestActivationGuardInputs directly without WS or fact-tag registration, exercising
 * every reachable decision branch without picker-pollution concerns. The Evaluate wrapper
 * is exercised through PIE walkthrough.
 */
struct FQuestActivationGuardInputs
{
    bool bIsContainer                  = false;
    bool bIsLive                       = false;
    bool bIsPendingGiver               = false;
    bool bIsBlocked                    = false;
    bool bBypassGiverGate              = false;
    bool bHasRegisteredGiver           = false;

    /**
     * Valid only when bIsContainer && bHasRegisteredGiver && !bBypassGiverGate. Computed
     * by Evaluate from UQuest::ReachableStepsByActivatePin against the entered Activate
     * pin's reachable Step subset; true when every reachable Step's Live fact is already
     * set. Drives the path-aware giver-gate skip (no work for the giver to enable).
     */
    bool bAllReachableStepsAlreadyLive = false;
};


/**
 * Activation-guard policy evaluator. Replaces the diamond + giver-gate + Block-gate cluster
 * inline in UQuestManagerSubsystem::ActivateNodeByTag with a free function that returns
 * EQuestActivationGuardDecision. The manager's ActivateNodeByTag becomes a switch over
 * the decision; existing handlers (logging, SetQuestPendingGiver, event publishes, watch
 * registration) stay verbatim, just relocated under their decision case.
 *
 * Two-tier surface:
 *   - DecideFromInputs(FQuestActivationGuardInputs) — pure logic, no SimpleCore deps.
 *     Unit-tested directly with synthesized bool combinations; covers every decision
 *     branch without WS or fact-tag fixtures.
 *   - Evaluate(Instance, NodeTag, IncomingOutcomeTag, bBypassGiverGate, bHasRegisteredGiver,
 *     WS) — runtime wrapper. Builds FQuestActivationGuardInputs from runtime state via
 *     FQuestLifecycleQuery primitives + UQuest::GetReachableStepsByActivatePin, then
 *     forwards to DecideFromInputs.
 *
 * Lives next to FQuestLifecycleQuery / FQuestTagComposer / FQuestCatchUpFanout in
 * Public/Utilities/. Distinct from FQuestLifecycleQuery: that namespace answers "is this
 * state asserted?" for any caller; this one answers "should this activation proceed?" for
 * the activation-cascade caller specifically. Shared underlying state-fact reads, distinct
 * questions — split per the SoT-per-functionality principle.
 */
namespace FQuestActivationGuard
{
    /** Pure decision over the inputs struct. No SimpleCore deps; safe to call from any context. */
    SIMPLEQUEST_API EQuestActivationGuardDecision DecideFromInputs(const FQuestActivationGuardInputs& In);

    /**
     * Builds the inputs from runtime state and returns the decision. Caller is responsible
     * for having pre-resolved the node instance (LoadedNodeInstances.Find) and the
     * giver-set membership (RegisteredGiverQuestTags.Contains). Defensive defaults: null
     * Instance or invalid NodeTag returns Proceed (preserves ActivateNodeByTag's current
     * degenerate-case behavior — invalid-tag activations short-circuit all WS-gated
     * checks and fall through to the downstream Activate call).
     */
    SIMPLEQUEST_API EQuestActivationGuardDecision Evaluate(
        const UWorldStateSubsystem* WS,
        const UQuestNodeBase* Instance,
        FGameplayTag NodeTag,
        FGameplayTag IncomingOutcomeTag,
        bool bBypassGiverGate,
        bool bHasRegisteredGiver);
}

