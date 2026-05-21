// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/StartQuestlineNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestlineStartRequestEvent.h"
#include "Signals/SignalSubsystem.h"

void UStartQuestlineNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent. The
	// questline-start request routes through the manager's HandleQuestlineStartRequest, which async-loads the
	// graph and applies Params to the entry Step's PendingActivationContext. Single mechanism shared with
	// USimpleQuestBlueprintLibrary::StartQuestline so graph-native and BP-callable paths converge.
	if (!Graph.IsNull())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
			{
				Signals->PublishMessage(Tag_Channel_QuestlineStartRequest, FQuestlineStartRequestEvent(Graph, Params));

				UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UStartQuestlineNode: published QuestlineStartRequest for '%s' (CustomData %s)"),
					*Graph.ToString(),
					Params.Dynamic.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));
			}
		}
	}
	else
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("UStartQuestlineNode: null Graph reference, skipping publish"));
	}

	ForwardActivation();
}