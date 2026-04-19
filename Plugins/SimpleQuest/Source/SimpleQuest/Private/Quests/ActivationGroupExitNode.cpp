// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/ActivationGroupExitNode.h"
#include "WorldState/WorldStateSubsystem.h"

void UActivationGroupExitNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent.
	ContextualTag = InContextualTag;

	if (GroupTag.IsValid())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (UWorldStateSubsystem* WS = GI->GetSubsystem<UWorldStateSubsystem>())
			{
				WS->AddFact(GroupTag);
			}
		}
	}

	ForwardActivation();
}