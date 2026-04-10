// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/GroupSignalSetterNode.h"
#include "WorldState/WorldStateSubsystem.h"

void UGroupSignalSetterNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent.
	ContextualTag = InContextualTag;

	if (GroupSignalTag.IsValid())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (UWorldStateSubsystem* WS = GI->GetSubsystem<UWorldStateSubsystem>())
			{
				WS->AddFact(GroupSignalTag);
			}
		}
	}

	ForwardActivation();
}
