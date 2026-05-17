// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/ClearBlockedNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestClearBlockRequestEvent.h"
#include "Signals/SignalSubsystem.h"

void UClearBlockedNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent. All clear-block
	// work (idempotency guard, Blocked fact clear, FQuestUnblockedEvent multi-publish, log) routes through the
	// manager's HandleClearBlockRequest by publishing on Tag_Channel_QuestClearBlockRequest. This keeps utility-
	// node-driven clears and BP-callable clears on a single mechanism — the manager handles both identically,
	// including multi-publish across asset-scoped alias tags for cross-asset subscribers. Deactivated is
	// intentionally not cleared here either; the target node's re-entry via its Activate input clears it.
	if (!TargetQuestTags.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
			{
				for (const FGameplayTag& Tag : TargetQuestTags)
				{
					Signals->PublishMessage(Tag_Channel_QuestClearBlockRequest,
						FQuestClearBlockRequestEvent(Tag, EDeactivationSource::Internal));

					UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UClearBlockedNode: '%s' — published ClearBlockRequest (source=Internal)"),
						*Tag.ToString());
				}
			}
		}
	}
	ForwardActivation();
}

