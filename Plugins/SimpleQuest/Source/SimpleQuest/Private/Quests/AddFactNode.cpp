// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/AddFactNode.h"

#include "SimpleQuestLog.h"
#include "Subsystems/WorldStateSubsystem.h"

void UAddFactNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility node, no Live or FQuestStartedEvent. Routes through the WorldState
	// subsystem's AddFact for each tag in Facts. WorldState handles the multi-perspective fact dispatch +
	// FWorldStateFactAddedEvent broadcast on the configured BroadcastMode.
	if (!Facts.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
			{
				for (const FGameplayTag& FactTag : Facts)
				{
					WorldState->AddFact(FactTag, BroadcastMode);
					UE_LOG(LogSimpleQuestActivation, Verbose,
						TEXT("UAddFactNode: added fact '%s' (broadcastMode=%s)"),
						*FactTag.ToString(),
						*UEnum::GetValueAsString(BroadcastMode));
				}
			}
		}
	}

	ForwardActivation();
}