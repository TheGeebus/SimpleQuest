// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/DeactivateQuestNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestDeactivateRequestEvent.h"
#include "Subsystems/SignalSubsystem.h"

void UDeactivateQuestNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super - utility nodes do not write Active or publish FQuestStartedEvent. All teardown work
	// (lifecycle checks, fact removal, FQuestDeactivatedEvent multi-publish, the deactivation cascade) routes through
	// the manager's deactivate-request handler, so graph-driven and BP-callable deactivations stay one mechanism.
	if (!TargetQuestTags.IsEmpty())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
			{
				for (const FGameplayTag& Tag : TargetQuestTags)
				{
					Signals->PublishMessage(Tag_Channel_QuestDeactivateRequest,
						FQuestDeactivateRequestEvent(Tag, EDeactivationSource::Internal));

					UE_LOG(LogSimpleQuestActivation, Verbose,
						TEXT("UDeactivateQuestNode: '%s' - published DeactivateRequest (source=Internal)"), *Tag.ToString());
				}
			}
		}
	}
	ForwardActivation();
}

