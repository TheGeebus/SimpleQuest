// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestCatchUpFanout.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestStateSubsystem.h"


namespace FQuestCatchUpFanout
{
	TArray<FGameplayTag> EnumerateTagsForCatchUp(FGameplayTag SubscribedTag, const UQuestStateSubsystem* StateSubsystem)
	{
		if (!SubscribedTag.IsValid() || !StateSubsystem)
		{
			return {};
		}

		TArray<FGameplayTag> CatchUpTags = StateSubsystem->GetQuestTagsUnderPrefix(SubscribedTag);

		UE_LOG(LogSimpleQuestSubscription, Verbose,
			TEXT("FQuestCatchUpFanout::EnumerateTagsForCatchUp : '%s' fanned out to %d known quest tag(s)"),
			*SubscribedTag.ToString(), CatchUpTags.Num());

		return CatchUpTags;
	}
}