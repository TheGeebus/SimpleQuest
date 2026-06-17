// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestCatchUpFanout.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestStateSubsystem.h"


namespace FQuestCatchUpFanout
{
	TArray<FGameplayTag> EnumerateTagsForCatchUp(FGameplayTag SubscribedTag, const UQuestStateSubsystem* StateSubsystem, ESignalRoutingMode Routing)
	{
		if (!SubscribedTag.IsValid() || !StateSubsystem) return {};

		if (!FSignalRoutingDefaults::IncludesDescendants(Routing))
		{
			UE_LOG(LogSimpleQuestSubscription, Verbose,
				TEXT("FQuestCatchUpFanout::EnumerateTagsForCatchUp : '%s' ExactMatch routing — returning subscribed tag only"),
				*SubscribedTag.ToString());
			return { SubscribedTag };
		}

		TArray<FGameplayTag> CatchUpTags = StateSubsystem->GetQuestTagsUnderPrefix(SubscribedTag);

		// Mirror the live cascade's parent-first delivery order. GetQuestTagsUnderPrefix returns tags in TMap
		// iteration order (non-deterministic relative to the cascade); subscribers binding via Hierarchical
		// routing expect the asset/parent-tag event to land before descendants — which the live cascade does
		// naturally because PublishMessage on the questline tag completes its synchronous dispatch before
		// ActivateQuestlineGraph iterates entry tags and triggers content-node publishes. Sort by tag-string
		// length ascending so the subscribed tag (shortest under itself) lands first, then descendants in
		// depth order.
		CatchUpTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString().Len() < B.ToString().Len();
		});

		UE_LOG(LogSimpleQuestSubscription, Verbose,
			TEXT("FQuestCatchUpFanout::EnumerateTagsForCatchUp : '%s' Descendants routing — fanned out to %d known quest tag(s), sorted parent-first"),
			*SubscribedTag.ToString(), CatchUpTags.Num());

		return CatchUpTags;
	}
}