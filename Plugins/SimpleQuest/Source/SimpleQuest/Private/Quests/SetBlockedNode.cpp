// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/SetBlockedNode.h"

#include "Events/QuestDeactivateRequestEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
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
					const FName FactName = FQuestStateTagUtils::MakeStateFact(Tag, FQuestStateTagUtils::Leaf_Blocked);
					const FGameplayTag BlockedFact = UGameplayTagsManager::Get().RequestGameplayTag(FactName, false);
					if (BlockedFact.IsValid()) WS->AddFact(BlockedFact);
				}
				if (Signals)
				{
					Signals->PublishMessage(Tag_Channel_QuestDeactivateRequest, FQuestDeactivateRequestEvent(Tag, EDeactivationSource::Internal));
				}
			}
		}
	}
	ForwardActivation();
}

