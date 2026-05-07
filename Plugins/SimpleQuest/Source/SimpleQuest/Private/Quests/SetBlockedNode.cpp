// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/SetBlockedNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestBlockedEvent.h"
#include "Events/QuestDeactivateRequestEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestLifecycleQuery.h"
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
					// Idempotency guard: skip already-blocked targets. Without this, the WorldState fact ref-count
					// bumps on each pulse without firing FactAdded, but FQuestBlockedEvent below would broadcast on
					// every pulse — out of sync with the manager-handler path which guards the same way.
					if (BlockedFact.IsValid() && !FQuestLifecycleQuery::IsBlocked(WS, Tag))
					{
						WS->AddFact(BlockedFact);

						if (Signals)
						{
							Signals->PublishMessage(Tag, FQuestBlockedEvent(Tag, EDeactivationSource::Internal));
						}

						UE_LOG(LogSimpleQuest, Log, TEXT("USetBlockedNode: '%s' — Blocked fact added, FQuestBlockedEvent published (source=Internal)"),
							*Tag.ToString());
					}
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