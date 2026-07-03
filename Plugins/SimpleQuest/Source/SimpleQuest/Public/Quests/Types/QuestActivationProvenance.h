// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestActivationProvenance.generated.h"

/**
 * Provenance of a quest's most recent activation. Stamped explicitly at every start site so the registry
 * doesn't have to infer "how did this quest start?" from sibling-field validity at read time. Read by
 * catch-up subscribers and the Quest State Facts Panel; will be a save/load axis once that feature lands.
 */
UENUM(BlueprintType)
enum class EQuestActivationProvenance : uint8
{
	/** Default / unstamped. Treat as "the manager didn't supply a provenance" — older code paths or external test harnesses. */
	Unknown,

	/** Activated by a UQuestGiverComponent fulfilling FQuestGivenEvent through HandleGiveQuestEvent. */
	GiverGate,

	/** Activated by another node's outcome / forward / deactivation chain (NextNodesByOutcome, NextNodesOnAnyOutcome, NextNodesOnForward, NextNodesOnDeactivation). */
	ChainCascade,

	/** Activated by external code via FQuestActivationRequestEvent or USimpleQuestBlueprintLibrary equivalents. */
	ExternalAPI,

	/** Activated by an entry-tag fire at graph activation time (via ActivateQuestlineGraph). */
	InitialEntry,
	
	/**
	 * Re-instantiated from a save on load (via RestoreQuestlineGraph), not a fresh gameplay start. Objectives read this
	 * in OnObjectiveActivated to suppress first-activation side effects — actor spawns, one-shot audio, "quest started"
	 * UI — that already fired before the game was saved. The objective is rebuilt and its progress re-applied; from the
	 * player's perspective nothing "started."
	 */
	Restored,
};