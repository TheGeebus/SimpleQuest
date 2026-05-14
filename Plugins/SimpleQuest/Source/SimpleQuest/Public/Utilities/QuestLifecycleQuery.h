// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"


class UWorldStateSubsystem;


/**
 * Per-leaf and compound predicates over a quest's WorldState lifecycle facts. Centralizes the
 * FQuestTagComposer::ResolveStateFactTag + UWorldStateSubsystem::HasFact pattern so every site that asks
 * "is this state asserted?" routes through a single named predicate rather than hand-rolling the two-step
 * resolve+probe at the call site.
 *
 * Predicates are side-effect-free — no per-call logging, no tag registration, no allocation. Null WS or
 * invalid ContextualTag returns false (defensive default for early-shutdown / late-construction call sites).
 *
 * Distinct from FQuestActivationGuard, which evaluates compound activation policy (the diamond + giver-gate
 * + Block decision). These primitives answer the "is this state asserted?" question for callers regardless
 * of whether they're computing an activation guard, a catch-up branch, a designer query, or a deactivation
 * precondition.
 */
namespace FQuestLifecycleQuery
{
    /** True if QuestState.<Tag>.Live is asserted in WS. */
    SIMPLEQUEST_API bool IsLive(const UWorldStateSubsystem* WS, FGameplayTag QuestTag);

    /** True if QuestState.<Tag>.Completed is asserted in WS. Survives across loop iterations on re-resolvable quests. */
    SIMPLEQUEST_API bool IsCompleted(const UWorldStateSubsystem* WS, FGameplayTag QuestTag);

    /** True if QuestState.<Tag>.PendingGiver is asserted in WS — the giver-side waypoint between Activated and Live. */
    SIMPLEQUEST_API bool IsPendingGiver(const UWorldStateSubsystem* WS, FGameplayTag QuestTag);

    /** True if QuestState.<Tag>.Deactivated is asserted in WS. Cleared when the node re-enters via its Activate input. */
    SIMPLEQUEST_API bool IsDeactivated(const UWorldStateSubsystem* WS, FGameplayTag QuestTag);

    /** True if QuestState.<Tag>.Blocked is asserted in WS. ClearBlocked is the only path back. */
    SIMPLEQUEST_API bool IsBlocked(const UWorldStateSubsystem* WS, FGameplayTag QuestTag);

    /**
     * True if the quest currently has an active lifecycle to interrupt — Live or PendingGiver. Used by
     * SetQuestDeactivated as the precondition to its cleanup work: an "active lifecycle" is the answer to
     * "is there anything here for deactivation to undo?" Correctly handles loopable quests where Completed
     * may co-exist with a freshly-set Live.
     */
    SIMPLEQUEST_API bool HasActiveLifecycle(const UWorldStateSubsystem* WS, FGameplayTag QuestTag);

    /**
     * True if the quest is in a terminal state — Completed or Deactivated. Used by HandleResolveRequest's
     * already-terminal guard. Note that Completed alone does NOT preclude re-entry (loopable quests stay
     * Completed across iterations); the "is this re-resolve allowed?" check belongs at the resolution-request
     * layer, not the activation layer.
     */
    SIMPLEQUEST_API bool IsTerminal(const UWorldStateSubsystem* WS, FGameplayTag QuestTag);
}