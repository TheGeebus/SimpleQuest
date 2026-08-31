// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/QuestRewardModifier.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "SimpleQuestLog.h"
#include "Rewards/RewardScalingSource.h"
#include "Subsystems/QuestStateSubsystem.h"


bool UQuestRewardModifier::DispatchModifyGrant(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	// Through the UFunction thunk rather than calling _Implementation directly, so a Blueprint override actually runs.
	return ModifyGrant(Grant, Incoming);
}

bool UQuestRewardModifier::ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	// Pass-through rather than pure virtual: a modifier that does nothing is legal (if useless), and a Blueprint subclass
	// that forgets to override should not take the grant down with it.
	return true;
}

void UQuestRewardModifier::DispatchModifyPreview(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	ModifyPreview(Preview, AsIfActivating);
}

void UQuestRewardModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	// Pass-through, like ModifyGrant. A modifier that only cares about grants leaves the advertisement alone.
}

void UQuestRewardModifier::AddBlocker(FQuestRewardPreview& Preview, const FGameplayTag BlockerType, const FText& Description)
{
	Preview.Blockers.Add(FQuestRewardBlocker{ BlockerType, Description });
}

const UGameInstance* UQuestRewardModifier::FindGameInstanceForGrant(const FQuestRewardActivationContext& Context)
{
	const AActor* Actor = Context.Instigator.Get();
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	return World ? World->GetGameInstance() : nullptr;
}

int32 UQuestRewardModifier::GetCompletionCount(const FQuestRewardActivationContext& Context, const bool bThisCompletionAlreadyCounted) const
{
	if (!Context.ResolvingQuestTag.IsValid()) return INDEX_NONE;

	const UGameInstance* GameInstance = FindGameInstanceForGrant(Context);
	const UQuestStateSubsystem* StateSubsystem = GameInstance ? GameInstance->GetSubsystem<UQuestStateSubsystem>() : nullptr;
	if (!StateSubsystem) return INDEX_NONE;

	// No record at all is a real zero - a quest that has never resolved has resolved zero times.
	const FQuestResolutionRecord* Record = StateSubsystem->GetQuestResolution(Context.ResolvingQuestTag);
	const int32 Recorded = Record ? Record->GetCount() : 0;
	return bThisCompletionAlreadyCounted ? Recorded : Recorded + 1;
}

