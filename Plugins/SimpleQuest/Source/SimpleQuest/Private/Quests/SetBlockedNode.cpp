// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/SetBlockedNode.h"

#include "Events/QuestDeactivateRequestEvent.h"
#include "Signals/SignalSubsystem.h"

void USetBlockedNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent.
	ContextualTag = InContextualTag;

	if (!TargetQuestTags.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
			{
				for (const FGameplayTag& Tag : TargetQuestTags)
				{
					Signals->PublishMessage(Tag_Channel_QuestDeactivateRequest, FQuestDeactivateRequestEvent(Tag, true));
				}
			}
		}
	}
	ForwardActivation();
}
