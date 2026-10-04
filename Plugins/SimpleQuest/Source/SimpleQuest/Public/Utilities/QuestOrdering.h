// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UQuestStateSubsystem;

/**
 * One ordering for every surface that shows quest content in a list - the cascade, catch-up replay, the outliner,
 * the sidebar. Before this existed each of them invented its own, and they disagreed: the same chapter could appear
 * in one order live and another after a reload.
 *
 * THE RULE, IN FOUR KEYS:
 *
 *   1. STRUCTURE. An ancestor always precedes its descendants. Not a comparison - the hierarchy says it.
 *
 *   2. AUTHORED POSITION. Where the compiler's walk of the graph put them. ONE INTEGER CARRIES THE WHOLE AUTHORED
 *      ANSWER: the walk is depth-first from the graph's entry points, so a node is always numbered before anything
 *      it activates, and it visits each node's successors in Order Bias order, so a designer's bias is folded in
 *      before the number is ever stamped. Nothing here re-derives either half.
 *
 *   3. ARRIVAL. For content with no position - two questlines each started on its own - the one that started first
 *      comes first. Nothing in either asset refers to the other, so there is no authored order to read: when they
 *      began is the only truth available, and it is also exactly the order the player watched them appear in. Quest
 *      time, broken by an in-frame sequence, because the clock is sampled per frame and two starts a node apart
 *      share a stamp.
 *
 *   4. NATURAL ORDER. Everything left. Natural rather than plain alphabetical because plain alphabetical puts
 *      Chapter_10 between Chapter_1 and Chapter_2, and that is exactly the case this has to get right.
 *
 * *** WHY THIS IS A LIST OF KEYS AND NOT A SET OF RULES. *** Each step is an unconditional comparison of two
 * totally-ordered values, tried in a fixed sequence. That is the only shape that cannot contradict itself. An
 * earlier version asked questions instead - "does one of these activate the other", "do their biases differ" - and
 * two of those answers could disagree, yielding A before B, B before C, and C before A all at once. A sort handed a
 * comparison like that returns whatever its input order happens to make it return, which is the exact bug this
 * exists to prevent: the live run and a restore feed it in different orders. The test
 * SimpleQuest.Ordering.SortIsIndependentOfInputOrder holds that line, and it caught this.
 */
namespace FQuestOrdering
{
	/**
	 * What the comparison needs to know about one tag. Taken through a callable rather than read off a subsystem
	 * because there are two sources: the runtime registry on UQuestStateSubsystem, and the automation tests, which
	 * build a table directly so the rule can be exercised with no world, no subsystem, and no compile.
	 */
	struct FQuestOrderingKeys
	{
		/**
		 * Place in the authored progression, with precedence and Order Bias already folded together by the compiler's
		 * walk. INDEX_NONE for a tag that is not a compiled node - in practice a questline's own identity tag, since
		 * an asset started on its own has no place in anybody's walk. Sorts behind every real position.
		 */
		int32 AuthoredPosition = INDEX_NONE;

		/**
		 * Quest time of this tag's FIRST arrival, negative when it has never started. The first rather than the latest
		 * because this answers "when did it appear", and a container re-entered later did not appear twice. Unstarted
		 * sorts behind everything that has run.
		 */
		double EntryTime = -1.0;

		/** Tiebreak for EntryTime - see FQuestEntryArrival::EntrySequence. INDEX_NONE when there is no arrival. */
		int32 EntrySequence = INDEX_NONE;
	};

	using FOrderingKeyLookup = TFunctionRef<FQuestOrderingKeys(FGameplayTag)>;

	/** True when A should be presented before B. */
	SIMPLEQUEST_API bool Less(FGameplayTag A, FGameplayTag B, FOrderingKeyLookup GetKeys);

	/** Sorts in place by the rule above. */
	SIMPLEQUEST_API void SortTags(TArray<FGameplayTag>& Tags, FOrderingKeyLookup GetKeys);

	/**
	 * Runtime convenience over the subsystem's registry. A null subsystem reports no keys at all, so everything falls
	 * through to natural order - degraded but still deterministic, which is the right failure.
	 */
	SIMPLEQUEST_API bool Less(FGameplayTag A, FGameplayTag B, const UQuestStateSubsystem* QuestState);
	SIMPLEQUEST_API void SortTags(TArray<FGameplayTag>& Tags, const UQuestStateSubsystem* QuestState);

	/** What the compiler's walk reads to order one node's successors. The three things a designer actually sets. */
	struct FAuthoredSiblingKeys
	{
		/** Designer's override. Higher sorts earlier; 0 is neutral. */
		int32 OrderBias = 0;

		/** Canvas position. Both MAX_int32 for a node with no editor twin, sending it to the end of its group. */
		int32 LayoutY = MAX_int32;
		int32 LayoutX = MAX_int32;
	};

	/**
	 * How the compiler's walk orders one node's successors, which is the single place Order Bias is applied. Separate
	 * from the rule above because it runs BEFORE positions exist - it is what produces them - so it can only read what
	 * the designer set directly.
	 *
	 * Higher bias first. Then top-to-bottom, left-to-right, because that is how these graphs are already arranged:
	 * parallel branches stacked, flow running across. *** THE LAYOUT IS A SETTING THE DESIGNER MAKES WITHOUT BEING
	 * ASKED TO, *** so reading it means ordering costs no new property in the common case and Order Bias is only
	 * reached for where it is wrong. The name settles the rest - a synthesized utility node cannot be placed, and two
	 * nodes can sit exactly on top of each other.
	 *
	 * Three totally-ordered keys over a total order on names, for the same reason the runtime rule is shaped that way.
	 */
	SIMPLEQUEST_API bool AuthoredSiblingLess(const FAuthoredSiblingKeys& A, const FAuthoredSiblingKeys& B, FName NameA, FName NameB);
}

