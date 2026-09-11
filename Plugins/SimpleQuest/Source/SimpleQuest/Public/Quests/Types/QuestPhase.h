// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestPhase.generated.h"

/**
 * Where a quest node is in its lifecycle right now, as one value. Named after the lifecycle events so a phase reads as
 * "the furthest event the node's CURRENT run has fired": an observer that subscribed this instant would be caught up to
 * exactly this point - Activated replays ACTIVATED, Started replays ACTIVATED then STARTED, and so on.
 *
 * Precedence when facts coexist is by how current they are, not by how far the lifecycle got. Live and PendingGiver
 * describe current state. Deactivated is cleared the moment a node re-enters, so its presence is current too. Completed and Started
 * are append-only anchors that survive a re-run, so they rank last and are also reported as flags on FQuestPhaseSnapshot
 * (bHasResolved / bHasStarted) for consumers that care about history.
 */
UENUM(BlueprintType)
enum class EQuestPhase : uint8
{
	/** No lifecycle fact on this tag - never reached this session, or reset. The absence of every other phase. */
	NotReached,

	/** Execution reached a giver-gated node and it is waiting to be given (PendingGiver). ACTIVATED has fired; STARTED has not. */
	Activated,

	/** Running (Live) - or a container that has run and is between children: started, nothing Live, nothing resolved. */
	Started,

	/** Interrupted and not re-entered since (Deactivated). If it had run, STARTED fired before the interrupt. */
	Deactivated,

	/** Resolved with an outcome and nothing current since (Completed). */
	Completed,
};

/**
 * One read of a node's lifecycle: the phase plus the facts that coexist with a phase rather than replace it. Returned by
 * UQuestStateSubsystem::GetQuestPhase and the Blueprint library's Get Quest Phase. It is the same read the catch-up pass
 * makes when an observer subscribes late, so a status built from this and a status built from events cannot disagree.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestPhaseSnapshot
{
	GENERATED_BODY()

	/** The current phase - see EQuestPhase for the precedence. */
	UPROPERTY(BlueprintReadOnly)
	EQuestPhase Phase = EQuestPhase::NotReached;

	/** Activated phase only: prerequisites are satisfied, so a give would succeed now. The condition behind ENABLED. */
	UPROPERTY(BlueprintReadOnly)
	bool bEnabled = false;

	/** The Blocked fact is set. Coexists with any phase; ClearBlocked is the only way back. */
	UPROPERTY(BlueprintReadOnly)
	bool bBlocked = false;

	/** Has gone Live at least once this session (the append-only Started anchor). Stays true across Completed and Deactivated. */
	UPROPERTY(BlueprintReadOnly)
	bool bHasStarted = false;

	/** Has resolved at least once this session (the Completed fact). Stays true while a repeatable quest runs again. */
	UPROPERTY(BlueprintReadOnly)
	bool bHasResolved = false;

	/** Outcome of the most recent resolution. Empty when never resolved, or resolved without naming an outcome. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag LatestOutcome;
};

