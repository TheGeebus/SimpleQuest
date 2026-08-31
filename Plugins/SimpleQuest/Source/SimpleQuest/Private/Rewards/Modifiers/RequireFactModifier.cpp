// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Rewards/Modifiers/RequireFactModifier.h"

#include "Engine/GameInstance.h"
#include "SimpleQuestLog.h"
#include "Quests/Types/QuestRewardBlockerTags.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"

void URequireFactModifier::WarnIfGatedOnQuestState() const
{
	if (bWarnedAboutQuestStateFact || !RequiredFact.IsValid()) return;
	if (!RequiredFact.ToString().StartsWith(FQuestTagComposer::StateNamespace)) return;

	bWarnedAboutQuestStateFact = true;
	UE_LOG(LogSimpleQuestActivation, Warning,
		TEXT("%s is gated on '%s', which is framework quest state advanced BY completions. This modifier reads the "
			"present, so its advertisement will sit one completion behind what it grants. Use Require Completion Count "
			"to gate on how many times a quest has resolved."),
		*GetClass()->GetName(), *RequiredFact.ToString());
}

int32 URequireFactModifier::GetFactCount(const FQuestRewardActivationContext& Context) const
{
	if (!RequiredFact.IsValid()) return INDEX_NONE;

	const UGameInstance* GameInstance = FindGameInstanceForGrant(Context);
	const UWorldStateSubsystem* WorldState = GameInstance ? GameInstance->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	if (!WorldState) return INDEX_NONE;

	// A fact never asserted reads zero, which is a real answer rather than a failure - the gate simply does not pass.
	return WorldState->GetFactValue(RequiredFact);
}

bool URequireFactModifier::PassesGate(const int32 FactCount) const
{
	return (FactCount >= MinimumCount) != bRequireAbsent;
}

bool URequireFactModifier::ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming)
{
	WarnIfGatedOnQuestState();

	const int32 FactCount = GetFactCount(Incoming);
	if (FactCount == INDEX_NONE)
	{
		// Granting is the safer guess than dropping: a reward wrongly withheld is invisible to the player and
		// unrecoverable. Warning rather than Verbose because a grant happens once, and because an unset RequiredFact is
		// an authoring mistake nobody would otherwise be told about.
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("%s: cannot evaluate its fact gate (no fact authored, or no reachable WorldStateSubsystem) - granted anyway."),
			*GetClass()->GetName());
		return true;
	}

	if (!PassesGate(FactCount))
	{
		UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("%s: '%s' is at %d, gate requires %s %d - grant dropped."),
			*GetClass()->GetName(),
			*RequiredFact.ToString(),
			FactCount,
			bRequireAbsent ? TEXT("under") : TEXT("at least"),
			MinimumCount);
		return false;
	}
	return true;
}

void URequireFactModifier::ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating)
{
	WarnIfGatedOnQuestState();

	const int32 FactCount = GetFactCount(AsIfActivating);
	if (FactCount == INDEX_NONE || PassesGate(FactCount)) return;

	AddBlocker(Preview, TAG_RewardBlocker_MissingFact.GetTag(),
		FText::Format(NSLOCTEXT("SimpleQuest", "RewardBlockedMissingFact", "Requires {0}"),
			FText::FromString(FQuestTagComposer::GetLeafSegment(RequiredFact.GetTagName()))));
}

