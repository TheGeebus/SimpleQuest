// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestTagComposer.h"
#include "WorldState/WorldStateSubsystem.h"


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
	bool IsCompleted (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Completed); }
	bool IsPendingGiver (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::PendingGiver); }
	bool IsDeactivated (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Deactivated); }
	bool IsBlocked (const UWorldStateSubsystem* WS, FGameplayTag QuestTag) { return ProbeLeaf(WS, QuestTag, EQuestStateLeaf::Blocked); }

	bool HasActiveLifecycle(const UWorldStateSubsystem* WS, FGameplayTag QuestTag)
	{
		return IsLive(WS, QuestTag) || IsPendingGiver(WS, QuestTag);
	}

	bool IsTerminal(const UWorldStateSubsystem* WS, FGameplayTag QuestTag)
	{
		return IsCompleted(WS, QuestTag) || IsDeactivated(WS, QuestTag);
	}
}