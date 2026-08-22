// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestActivationBlocker.generated.h"

/**
 * Reason a quest is not currently activatable / startable. One enum value per distinct cause; the structured
 * FQuestActivationBlocker carries the enum plus any contextual data (e.g., which prereq leaves are unsatisfied
 * for the PrereqUnmet case). Returned in arrays by UQuestManagerSubsystem::QueryQuestActivationBlockers and
 * FQuestGiveBlockedEvent so designer-side dialogue / UI can surface contextual refusal text.
 */
UENUM(BlueprintType)
enum class EQuestActivationBlocker : uint8
{
	/** One or more prereq leaves are unsatisfied. UnsatisfiedLeafTags lists which. */
	PrereqUnmet,

	/** Quest's Blocked fact is set - externally locked out via SetBlocked. ClearBlocked required to re-enable. */
	Blocked,

	/** Quest's Live fact is set - already running. Cannot be re-given while active. */
	AlreadyLive,

	/** Quest is not in a giver-gated PendingGiver state - the giver isn't currently offering it. */
	NotPendingGiver,

	/** ContextualTag isn't registered in the runtime tag manager. Stale or never-compiled tag. */
	UnknownQuest,

	/**
	 * Quest IS already in PendingGiver state and a re-activation cascade tried to re-fire the gate. Surfaced as
	 * an FQuestActivationFailedEvent reason; not used by give-time blocker queries (the give-flow checks
	 * NotPendingGiver as its symmetric partner - that's "no gate to consume", this is "gate still pending").
	 */
	AlreadyPendingGiver,

	/**
	 * An advancement hold is active on this quest, or on a container above it. The quest is otherwise startable - this
	 * is a deliberate pause requested by game code, not a failure. Held activations are parked and replay in arrival
	 * order when the last hold clears, so a node reporting this is waiting rather than refused.
	 */
	HeldForAdvancement,
};

/**
 * Structured "why can't this quest be started right now" entry. Returned in arrays from the activation-blocker
 * query API and the give-blocked event. One entry per distinct blocker condition; an empty array means the
 * quest is currently startable.
 *
 * UnsatisfiedLeafTags is populated only when Reason == PrereqUnmet; for other reasons the array is empty.
 * Designers consuming this branch on Reason and produce contextual dialogue / UI accordingly.
 */
USTRUCT(BlueprintType, meta = (ScriptName = "EQuestActivationBlocker"))
struct SIMPLEQUEST_API FQuestActivationBlocker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest|Blocker")
	EQuestActivationBlocker Reason = EQuestActivationBlocker::PrereqUnmet;

	/** For Reason == PrereqUnmet: leaf tags that evaluated false. Empty for all other reasons. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest|Blocker")
	TArray<FGameplayTag> UnsatisfiedLeafTags;
};

/**
 * One refused activation attempt, appended to a quest's refusal history. Session-scoped and deliberately NOT SaveGame:
 * refusals are diagnostic history rather than progression state, and a save that carried them would grow without bound.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestRefusalEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest|Blocker")
	EQuestActivationBlocker Reason = EQuestActivationBlocker::PrereqUnmet;

	/** World time the refusal occurred, in the manager's GetTimeSeconds domain. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest|Blocker")
	double RefusalTime = 0.0;
};

/**
 * Per-quest refusal history, the durable counterpart to FQuestActivationFailedEvent. An event reaches only whoever was
 * subscribed when it fired; this is what a surface that arrived afterwards - a panel, a joining client - can still read.
 *
 * Bounded, because a refusal repeats every time a player retries a blocked interaction and unbounded growth would be a
 * leak on exactly the input a frustrated player produces most of.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestRefusalRecord
{
	GENERATED_BODY()

	/** Most recent last. Capped at MaxEntries; oldest are dropped. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest|Blocker")
	TArray<FQuestRefusalEntry> History;

	static constexpr int32 MaxEntries = 16;
};

