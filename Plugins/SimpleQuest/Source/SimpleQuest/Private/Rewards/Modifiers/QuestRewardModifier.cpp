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

bool UQuestRewardModifier::DispatchModifyPreview(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	return ModifyPreview(Preview, AsIfActivating);
}

bool UQuestRewardModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	// Pass-through, like ModifyGrant. A modifier that only cares about grants leaves the advertisement alone rather
	// than hiding it - silence here would make a reward vanish from "do this, get this" for no stated reason.
	return true;
}





