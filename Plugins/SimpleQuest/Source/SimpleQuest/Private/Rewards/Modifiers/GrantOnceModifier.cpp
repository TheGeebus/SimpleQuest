// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/GrantOnceModifier.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestStateSubsystem.h"


int32 UGrantOnceModifier::GetResolutionCount(const FQuestRewardActivationContext& Context) const
{
	if (!Context.ResolvingQuestTag.IsValid()) return INDEX_NONE;

	// Reach the game through the Instigator rather than this modifier's own GetWorld(), which is null: a modifier is a
	// subobject of a reward on a questline graph ASSET, and an asset has no world. Same route UScaleByRecipientModifier
	// takes, and the reason both paths need an actor at all before they can ask the game anything.
	const AActor* Actor = Context.Instigator.Get();
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UQuestStateSubsystem* StateSubsystem = GameInstance ? GameInstance->GetSubsystem<UQuestStateSubsystem>() : nullptr;
	if (!StateSubsystem) return INDEX_NONE;

	// No record at all is a real zero rather than a failure - a quest that has never resolved has resolved zero times,
	// which is exactly what both callers want to hear.
	const FQuestResolutionRecord* Record = StateSubsystem->GetQuestResolution(Context.ResolvingQuestTag);
	return Record ? Record->GetCount() : 0;
}

bool UGrantOnceModifier::ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	const int32 ResolutionCount = GetResolutionCount(Incoming);
	if (ResolutionCount == INDEX_NONE)
	{
		// Granting is the safer guess than dropping: a reward wrongly withheld is invisible to the player and
		// unrecoverable, while a reward wrongly repeated is visible and can be taken back. Warning rather than Verbose
		// because a grant happens once, and a silently ungated payout is the exact failure this class exists to prevent.
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("%s: cannot establish a prior-resolution count for this grant (no resolving quest on the context, or no "
				 "reachable QuestStateSubsystem) - granted anyway."), *GetClass()->GetName());
		return true;
	}

	// *** THE RESOLUTION DRIVING THIS GRANT IS ALREADY RECORDED. *** PublishGraphResolutions calls RecordResolution before
	// it grants, so a legitimate first grant sees a count of exactly one and only a repeat sees more. Comparing against
	// zero here would drop every grant including the first.
	if (ResolutionCount > 1)
	{
		UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("%s: '%s' has resolved %d time(s) - grant dropped."),
			*GetClass()->GetName(), *Incoming.ResolvingQuestTag.ToString(), ResolutionCount);
		return false;
	}

	return true;
}

bool UGrantOnceModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	const int32 ResolutionCount = GetResolutionCount(AsIfActivating);
	if (ResolutionCount == INDEX_NONE)
	{
		// Verbose, not Warning: a tooltip can ask this every frame, and an editor summary asks it with no viewer at all
		// (SGraphNode_LinkedQuestline passes nullptr). Advertising is the right answer there - a graph node should show
		// what a questline CAN grant, not what some absent player already collected.
		UE_LOG(LogSimpleQuestActivation, Verbose,
			TEXT("%s: cannot establish a prior-resolution count for this preview - advertised anyway."), *GetClass()->GetName());
		return true;
	}

	// *** ONE LOWER THAN THE GRANT PATH, DELIBERATELY. *** Nothing is in flight when a preview is asked, so there is no
	// self to discount: any recorded resolution means this payout has already been made, and advertising it again
	// promises exactly what the grant will refuse.
	if (ResolutionCount >= 1)
	{
		UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("%s: '%s' already resolved %d time(s) - preview hidden."),
			*GetClass()->GetName(), *AsIfActivating.ResolvingQuestTag.ToString(), ResolutionCount);
		return false;
	}

	return true;
}