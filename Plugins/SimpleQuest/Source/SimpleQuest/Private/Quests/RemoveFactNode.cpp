// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/RemoveFactNode.h"

#include "SimpleQuestLog.h"
#include "Subsystems/WorldStateSubsystem.h"

void URemoveFactNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility node, no Live or FQuestStartedEvent. Routes through the WorldState
	// subsystem's RemoveFact for each tag in Facts. WorldState handles the multi-perspective fact dispatch +
	// FWorldStateFactRemovedEvent broadcast on the configured BroadcastMode.
	if (!Facts.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
			{
				for (const FGameplayTag& FactTag : Facts)
				{
					WorldState->RemoveFact(FactTag, BroadcastMode);
					UE_LOG(LogSimpleQuestActivation, Verbose,
						TEXT("URemoveFactNode: removed fact '%s' (broadcastMode=%s)"),
						*FactTag.ToString(),
						*UEnum::GetValueAsString(BroadcastMode));
				}
			}
		}
	}

	ForwardActivation();
}