// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/RequireCompletionCountModifier.h"

#include "SimpleQuestLog.h"
#include "Quests/Types/QuestRewardBlockerTags.h"

bool URequireCompletionCountModifier::PaysOnCompletion(const int32 Completion) const
{
	if (Completion < FirstCompletion) return false;
	return LastCompletion <= 0 || Completion <= LastCompletion;
}

bool URequireCompletionCountModifier::ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	const int32 Completion = GetCompletionCount(Incoming, true);
	if (Completion == INDEX_NONE)
	{
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("%s: cannot establish a completion count for this grant - granted anyway."), *GetClass()->GetName());
		return true;
	}

	if (!PaysOnCompletion(Completion))
	{
		UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("%s: completion #%d is outside [%d..%s] - grant dropped."),
			*GetClass()->GetName(),
			Completion,
			FirstCompletion,
			LastCompletion > 0 ? *FString::FromInt(LastCompletion) : TEXT("unbounded"));
		return false;
	}
	return true;
}

void URequireCompletionCountModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	// *** THE ONE PLACE A LOOKAHEAD SURVIVES, and it is sound here because the framework owns the number: a completion
	// advances resolution history by exactly one, so the run an advertisement describes is knowably the next one.
	const int32 Completion = GetCompletionCount(AsIfActivating, /*bThisCompletionAlreadyCounted*/ false);
	if (Completion == INDEX_NONE || PaysOnCompletion(Completion)) return;

	AddBlocker(Preview, TAG_RewardBlocker_CompletionCount.GetTag(),
		FText::Format(NSLOCTEXT("SimpleQuest", "RewardBlockedCompletionCount", "Available on run {0}"),
			FText::AsNumber(FirstCompletion)));
}

