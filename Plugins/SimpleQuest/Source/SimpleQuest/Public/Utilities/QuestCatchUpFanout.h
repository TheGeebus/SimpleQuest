// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"


class UQuestStateSubsystem;


/**
 * Helper for subscribers' catch-up paths. When a subscriber binds to a quest event channel mid-session, it needs
 * to fire its delegates immediately for any state already present at subscription time (the "catch-up" pass) —
 * Live, Completed, Deactivated, Blocked, PendingGiver. The historical implementation probed facts on the literal
 * subscribed tag and fired delegates only if those exact-tag facts were set, which broke for parent-prefix
 * subscriptions: live signals route hierarchically (publish-tag → root) so a parent-tag subscriber receives
 * descendant events going forward, but the catch-up probe finding nothing for the parent meant zero historical
 * recovery for descendants already in some state at bind time.
 *
 * This namespace centralizes the "what tags should the catch-up loop iterate?" decision so UQuestEventSubscription
 * (the BindToQuestEvent K2 node's runtime) and UQuestWatcherComponent share the logic and stay parity-aligned.
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
	 *   - SubscribedTag is an unknown parent prefix (namespace, e.g. SimpleQuest.Questline.Procedural): returns
	 *     all known descendants. Closes the parent-prefix-subscription gap §1.1 was scoped to fix.
	 *
	 * Null / invalid inputs return empty without warning — defensive default for early-shutdown / late-construction
	 * call sites (e.g. catch-up firing during world teardown after the state subsystem has already deinitialized).
	 */
	SIMPLEQUEST_API TArray<FGameplayTag> EnumerateTagsForCatchUp(FGameplayTag SubscribedTag, const UQuestStateSubsystem* StateSubsystem);
}