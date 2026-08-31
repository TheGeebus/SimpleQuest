// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/GrantOnceModifier.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SimpleQuestLog.h"
#include "Quests/Types/QuestRewardBlockerTags.h"


bool UGrantOnceModifier::ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	const int32 Completions = GetCompletionCount(Incoming, true);
	if (Completions == INDEX_NONE)
	{
		// Granting is the safer guess than dropping: a reward wrongly withheld is invisible and unrecoverable, while a
		// reward wrongly repeated is visible. Warning rather than Verbose because a grant happens once.
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("%s: cannot establish a completion count for this grant - granted anyway."), *GetClass()->GetName());
		return true;
	}

	if (Completions > 1)
	{
		UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("%s: '%s' completion #%d - grant dropped."),
			*GetClass()->GetName(), *Incoming.ResolvingQuestTag.ToString(), Completions);
		return false;
	}
	return true;
}

void UGrantOnceModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	// TRUE here, unlike the grant path's own reading: describing the PRESENT needs no lookahead. "Has this been
	// collected already" is answerable from the history as it stands, and that is the whole reason marking a preview
	// beats deciding for it - a description never has to predict.
	const int32 Completions = GetCompletionCount(AsIfActivating, /*bThisCompletionAlreadyCounted*/ true);
	if (Completions == INDEX_NONE || Completions < 1) return;

	AddBlocker(Preview, TAG_RewardBlocker_AlreadyGranted.GetTag(),
		NSLOCTEXT("SimpleQuest", "RewardBlockedAlreadyGranted", "Already collected"));
}

