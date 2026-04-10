// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/ClearBlockedNode.h"
#include "GameplayTagsManager.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"

void UClearBlockedNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent.
	ContextualTag = InContextualTag;

	if (!TargetQuestTags.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (UWorldStateSubsystem* WS = GI->GetSubsystem<UWorldStateSubsystem>())
			{
				for (auto Tag : TargetQuestTags)
				{
					const FName FactName = QuestStateTagUtils::MakeStateFact(Tag, QuestStateTagUtils::Leaf_Blocked);
					const FGameplayTag BlockedFact = UGameplayTagsManager::Get().RequestGameplayTag(FactName, false);
					if (BlockedFact.IsValid()) WS->ClearFact(BlockedFact);
					// Deactivated is intentionally not cleared here. The target node's re-entry via its Activate input clears
					// it; ClearBlocked only removes the permanent gate.
				}
			}
		}
	}
	ForwardActivation();
}
