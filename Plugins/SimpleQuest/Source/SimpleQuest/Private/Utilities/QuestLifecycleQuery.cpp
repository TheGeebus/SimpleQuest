// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestLifecycleQuery.h"

#include "Subsystems/QuestStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "Subsystems/WorldStateSubsystem.h"


namespace FQuestLifecycleQuery
{
	namespace
	{
		bool ProbeLeaf(const UWorldStateSubsystem* WS, FGameplayTag QuestTag, EQuestStateLeaf Leaf)
		{
			if (!WS || !QuestTag.IsValid()) return false;
			const FGameplayTag Fact = FQuestTagComposer::ResolveStateFactTag(QuestTag, Leaf);
			return Fact.IsValid() && WS->HasFact(Fact);
		}
	}

	bool IsLive (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Live); }
	bool IsStarted (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Started); }
	bool IsCompleted (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Completed); }
	bool IsPendingGiver (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::PendingGiver); }
	bool IsDeactivated (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Deactivated); }
	bool IsBlocked (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Blocked); }
	bool IsHeld (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Held); }

	bool HasActiveLifecycle(const UWorldStateSubsystem* WS, FGameplayTag QuestTag)
	{
		return IsLive(WS, QuestTag) || IsPendingGiver(WS, QuestTag);
	}

	bool IsTerminal(const UWorldStateSubsystem* WS, FGameplayTag QuestTag)
	{
		return IsCompleted(WS, QuestTag) || IsDeactivated(WS, QuestTag);
	}

	FQuestPhaseSnapshot GetPhase(const UWorldStateSubsystem* WS, const UQuestStateSubsystem* QSS, FGameplayTag QuestTag)
	{
		FQuestPhaseSnapshot Out;
		if (!WS || !QuestTag.IsValid()) return Out;

		const bool bLive         = IsLive(WS, QuestTag);
		const bool bPendingGiver = IsPendingGiver(WS, QuestTag);
		const bool bDeactivated  = IsDeactivated(WS, QuestTag);
		Out.bHasStarted  = IsStarted(WS, QuestTag);
		Out.bHasResolved = IsCompleted(WS, QuestTag);
		Out.bBlocked     = IsBlocked(WS, QuestTag);

		// Ranked by how current each fact is. Live and PendingGiver are transient and describe now; Deactivated is cleared on
		// re-entry, so if it is set it is current; Completed and Started are append-only and survive a re-run, so they come
		// last - a repeatable quest running again reads Started here and reports its earlier completion through bHasResolved.
		if      (bLive)            Out.Phase = EQuestPhase::Started;
		else if (bPendingGiver)    Out.Phase = EQuestPhase::Activated;
		else if (bDeactivated)     Out.Phase = EQuestPhase::Deactivated;
		else if (Out.bHasResolved) Out.Phase = EQuestPhase::Completed;
		else if (Out.bHasStarted)  Out.Phase = EQuestPhase::Started;
		else                       Out.Phase = EQuestPhase::NotReached;

		if (QSS)
		{
			if (bPendingGiver)    Out.bEnabled      = QSS->GetQuestPrereqStatus(QuestTag).bSatisfied;
			if (Out.bHasResolved) Out.LatestOutcome = QSS->GetLatestResolution(QuestTag).OutcomeTag;
		}
		return Out;
	}
}

