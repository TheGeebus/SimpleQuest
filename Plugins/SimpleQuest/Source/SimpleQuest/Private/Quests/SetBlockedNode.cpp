// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/SetBlockedNode.h"

#include "Events/QuestDeactivateRequestEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "WorldState/WorldStateSubsystem.h"

void USetBlockedNode::ActivateInternal(FGameplayTag InContextualTag)
{
	ContextualTag = InContextualTag;

	if (!TargetQuestTags.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			UWorldStateSubsystem* WS = GI->GetSubsystem<UWorldStateSubsystem>();
			USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>();

			for (const FGameplayTag& Tag : TargetQuestTags)
			{
				if (WS)
				{
					const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(Tag, EQuestStateLeaf::Blocked);
					if (BlockedFact.IsValid()) WS->AddFact(BlockedFact);
				}
				// Block is purely a re-entry gate by default — leaves any in-flight lifecycle on the target alone.
				// Designers opt into interrupting in-flight quests via the bAlsoDeactivateTargets toggle.
				if (bAlsoDeactivateTargets && Signals)
				{
					Signals->PublishMessage(Tag_Channel_QuestDeactivateRequest, FQuestDeactivateRequestEvent(Tag, EDeactivationSource::Internal));
				}
			}
		}
	}
	ForwardActivation();
}

