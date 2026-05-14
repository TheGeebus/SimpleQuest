// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/SetBlockedNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestBlockRequestEvent.h"
#include "Events/QuestDeactivateRequestEvent.h"
#include "Signals/SignalSubsystem.h"

void USetBlockedNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent. All block-side
	// work (idempotency guard, Blocked fact write, FQuestBlockedEvent multi-publish, log) routes through the
	// manager's HandleBlockRequest by publishing on Tag_Channel_QuestBlockRequest. This keeps utility-node-driven
	// blocks and BP-callable blocks on a single mechanism — the manager handles both identically, including
	// multi-publish across asset-scoped alias tags for cross-asset subscribers.
	if (!TargetQuestTags.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
			{
				for (const FGameplayTag& Tag : TargetQuestTags)
				{
					Signals->PublishMessage(Tag_Channel_QuestBlockRequest,
						FQuestBlockRequestEvent(Tag, EDeactivationSource::Internal));

					// Block is purely a re-entry gate by default — leaves any in-flight lifecycle on the target alone.
					// Designers opt into interrupting in-flight quests via the bAlsoDeactivateTargets toggle.
					if (bAlsoDeactivateTargets)
					{
						Signals->PublishMessage(Tag_Channel_QuestDeactivateRequest,
							FQuestDeactivateRequestEvent(Tag, EDeactivationSource::Internal));
					}

					UE_LOG(LogSimpleQuest, Verbose, TEXT("USetBlockedNode: '%s' — published BlockRequest%s (source=Internal)"),
						*Tag.ToString(),
						bAlsoDeactivateTargets ? TEXT(" + DeactivateRequest") : TEXT(""));
				}
			}
		}
	}
	ForwardActivation();
}

