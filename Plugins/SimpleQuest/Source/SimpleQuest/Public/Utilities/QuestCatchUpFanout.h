// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Signals/Types/SignalRoutingFlags.h"
#include "Quests/Types/QuestEventPayload.h"
#include "Quests/Types/QuestEventTypes.h"


class AActor;
class UQuestStateSubsystem;
class UWorldStateSubsystem;

/**
 * Helper for subscribers' catch-up paths. When a subscriber binds to a quest event channel mid-session, it needs
 * to fire its delegates immediately for any state already present at subscription time (the "catch-up" pass) —
 * Live, Completed, Deactivated, Blocked, PendingGiver. The historical implementation probed facts on the literal
 * subscribed tag and fired delegates only if those exact-tag facts were set, which broke for parent-prefix
 * subscriptions: live signals route hierarchically (publish-tag → root) so a parent-tag subscriber receives
 * descendant events going forward, but the catch-up probe finding nothing for the parent meant zero historical
 * recovery for descendants already in some state at bind time.
 *
 * This namespace centralizes the "what tags should the catch-up loop iterate?" decision so UQuestLifecycleObserver
 * (the ObserveQuestLifecycle K2 node's runtime) and UQuestObserverComponent share the logic and stay parity-aligned.
 * Both subscribers call EnumerateTagsForCatchUp and iterate the result, firing per-tag synthetic-context broadcasts.
 *
 * Lives next to FQuestTagComposer (Public/Utilities/) — same module, same architectural tier (consumer-side
 * helpers around the data registries), distinct from FQuestTagComposer itself which deals in tag string composition.
 */
namespace FQuestCatchUpFanout
{
	/**
	 * Returns every known quest tag that matches SubscribedTag or is a descendant of SubscribedTag. Caller iterates
	 * the result and fact-probes each tag in turn. Empty array when SubscribedTag isn't a known quest tag and has
	 * no known descendants — subscribers correctly produce zero broadcasts in that case (same effective behavior
	 * as the legacy exact-tag probe on an unregistered tag).
	 *
	 * Three cases the helper handles uniformly through GetQuestTagsUnderPrefix's MatchesTag semantics:
	 *   - SubscribedTag is a known leaf quest (Step / non-wrapper): returns [SubscribedTag] only.
	 *   - SubscribedTag is a known wrapper (UQuest container) with inner Steps: returns the wrapper plus all
	 *     descendant Step / wrapper tags. Mirrors the signal bus's hierarchical broadcast — live events from
	 *     descendants reach a parent-tag subscriber, so catch-up should reach those descendants too.
	 *   - SubscribedTag is an unknown parent prefix (namespace, e.g. SimpleQuest.Questline.ine.Procedural): returns
	 *     all known descendants. Closes the parent-prefix-subscription gap §1.1 was scoped to fix.
	 *
	 * Null / invalid inputs return empty without warning — defensive default for early-shutdown / late-construction
	 * call sites (e.g. catch-up firing during world teardown after the state subsystem has already deinitialized).
	 */
	SIMPLEQUEST_API TArray<FGameplayTag> EnumerateTagsForCatchUp(FGameplayTag SubscribedTag, const UQuestStateSubsystem* StateSubsystem, ESignalRoutingMode Routing = FSignalRoutingDefaults::HierarchicalSubscribe);

	
	/**
	 * One reconstructable lifecycle event for a caught-up tag, plus the extras recovered from the registries. Plain
	 * (non-reflected) — consumed only by the C++ catch-up call sites, never crosses a BP boundary.
	 */
	struct FReconstructedEvent
	{
		EQuestLifecycleEventType EventType = EQuestLifecycleEventType::None;
		FGameplayTag OutcomeTag;            // Completed only — recovered from the resolution registry
		AActor* RecoveredGiver = nullptr;   // Started only — recovered last-giver actor
	};

	/**
	 * Everything a subscriber needs to REPLAY one fanned-out tag from persisted state — the parity-critical half:
	 * matched channel, rehydrated payload, prereq status, and which lifecycle events the state supports, in canonical
	 * fire order. Pure data, no delegate knowledge: emission and per-subscriber gating (exposure flags, outcome
	 * filter, live-dedup, bookkeeping) stay at the call site.
	 */
	struct FTagReconstruction
	{
		FGameplayTag MatchedChannel;
		FQuestEventPayload Payload;          // fully rehydrated, Delivery = CatchUp
		FQuestPrereqStatus PrereqStatus;     // for the Activated / Enabled delegate args
		TArray<FReconstructedEvent> Events;  // state-eligible events, canonical fire order
	};

	/**
	 * Builds the shared reconstruction for one canonical tag: matched channel, payload rehydrated from the persisted
	 * entry snapshot + display registry, prereq status, and the ordered lifecycle events the current state supports
	 * replaying (each with its recovered outcome / giver). THE single source of truth both catch-up subscribers share
	 * — every future catch-up rule (a new state anchor, another recovered payload field) lands here once, not mirrored.
	 * Returns an empty reconstruction for null WorldState / invalid tag.
	 */
	SIMPLEQUEST_API FTagReconstruction ReconstructTag(const FGameplayTag& CanonicalTag, const FGameplayTag& SubscribedTag,
		UWorldStateSubsystem* WorldState, UQuestStateSubsystem* QuestState);
}
