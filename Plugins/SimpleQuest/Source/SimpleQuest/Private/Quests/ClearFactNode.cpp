// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/ClearFactNode.h"

#include "SimpleQuestLog.h"
#include "WorldState/WorldStateSubsystem.h"

void UClearFactNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility node, no Live or FQuestStartedEvent. Routes through the WorldState
	// subsystem's ClearFact, which hard-removes the entry regardless of ref-count and broadcasts the boundary
	// event by default. Distinct from Remove Fact (which decrements; only removes on 1→0).
	if (!Facts.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
			{
				for (const FGameplayTag& FactTag : Facts)
				{
					WorldState->ClearFact(FactTag, bSuppressBroadcast);
					UE_LOG(LogSimpleQuestActivation, Verbose,
						TEXT("UClearFactNode: cleared fact '%s' (suppressBroadcast=%s)"),
						*FactTag.ToString(),
						bSuppressBroadcast ? TEXT("true") : TEXT("false"));
				}
			}
		}
	}

	ForwardActivation();
}