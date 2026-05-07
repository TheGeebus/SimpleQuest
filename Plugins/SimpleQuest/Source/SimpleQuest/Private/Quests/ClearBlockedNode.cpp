// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/ClearBlockedNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestUnblockedEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestTagComposer.h"
#include "WorldState/WorldStateSubsystem.h"

void UClearBlockedNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent.
	ContextualTag = InContextualTag;

	if (!TargetQuestTags.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			UWorldStateSubsystem* WS = GI->GetSubsystem<UWorldStateSubsystem>();
			USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>();

			for (auto Tag : TargetQuestTags)
			{
				if (WS)
				{
					const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(Tag, EQuestStateLeaf::Blocked);
					// Idempotency guard: skip targets that aren't currently blocked. Symmetric with USetBlockedNode's
					// guard and the manager-handler path; keeps FQuestUnblockedEvent emission aligned with genuine
					// fact transitions only.
					if (BlockedFact.IsValid() && FQuestLifecycleQuery::IsBlocked(WS, Tag))
					{
						WS->ClearFact(BlockedFact);
						// Deactivated is intentionally not cleared here. The target node's re-entry via its Activate input
						// clears it; ClearBlocked only removes the permanent gate.

						if (Signals)
						{
							Signals->PublishMessage(Tag, FQuestUnblockedEvent(Tag, EDeactivationSource::Internal));
						}

						UE_LOG(LogSimpleQuest, Log, TEXT("UClearBlockedNode: '%s' — Blocked fact cleared, FQuestUnblockedEvent published (source=Internal)"),
							*Tag.ToString());
					}
				}
			}
		}
	}
	ForwardActivation();
}