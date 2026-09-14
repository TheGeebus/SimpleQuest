// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Rewards/Modifiers/QuestRewardModifier.h"
#include "RequireFactModifier.generated.h"

/**
 * Reference modifier: drops a grant unless a WorldState fact holds. Attach it to a reward that is conditional on
 * something the game already tracks - guild membership, a difficulty setting, a story beat reached - written in the
 * same fact vocabulary prerequisites already use, so gating a REWARD asks nothing a designer has not already learned
 * in order to gate a STEP.
 *
 * COUNTS, NOT MERELY PRESENCE. WorldState facts are reference-counted, so MinimumCount asks "at least this many" and
 * the default of 1 is exactly the has-it-or-not case.
 *
 * IT DESCRIBES THE PRESENT, which is exactly what an advertisement asks of it: a reward gated on a fact the player has
 * yet to earn comes back marked "Requires <fact>" rather than hidden, and the mark clears the moment they earn it.
 *
 * FOR GATING ON QUEST PROGRESS, USE Require Completion Count. This modifier cannot predict what a completion will do to
 * an arbitrary world fact, so gating on a fact that completions advance describes a state one completion behind. The
 * warning below fires when that is what you have done.
 */
UCLASS(meta = (DisplayName = "Require Fact"))
class SIMPLEQUEST_API URequireFactModifier : public UQuestRewardModifier
{
	GENERATED_BODY()

protected:
	/** The fact to test. Any tag the game writes to WorldState - the same vocabulary prerequisites read. */
	UPROPERTY(EditAnywhere, Category = "Modifier")
	FGameplayTag RequiredFact;

	/**
	 * How many times the fact must have been asserted. 1 is plain presence and is almost always what is wanted; a
	 * higher number suits a fact the game adds repeatedly - relics collected, floors cleared.
	 */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"), Category = "Modifier")
	int32 MinimumCount = 1;

	/** Inverts the test, so the grant survives only while the fact sits BELOW MinimumCount. */
	UPROPERTY(EditAnywhere, Category = "Modifier")
	bool bRequireAbsent = false;

	virtual bool ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) override;
	virtual void ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating) override;

private:
	/**
	 * Current count for RequiredFact, or INDEX_NONE when it cannot be established. Deliberately NOT normalized to the
	 * completion the way the base class's GetCompletionCount is - there is nothing to normalize against, which is the
	 * whole limitation stated above.
	 */
	int32 GetFactCount(const FQuestRewardActivationContext& Context) const;

	/** Shared verdict, so the grant and the advertisement cannot disagree about the same reading. */
	bool PassesGate(int32 FactCount) const;

	/** Warns once per instance when gated on framework quest state, where the reading is knowably one behind. */
	void WarnIfGatedOnQuestState() const;

	/** One warning per instance per session; the check is per-evaluation and a tooltip evaluates constantly. */
	mutable bool bWarnedAboutQuestStateFact = false;
};

