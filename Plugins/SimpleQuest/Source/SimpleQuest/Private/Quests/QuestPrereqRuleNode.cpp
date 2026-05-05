// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestPrereqRuleNode.h"

#include "SimpleQuestLog.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Events/QuestResolutionRecordedEvent.h"
#include "Events/QuestEntryRecordedEvent.h"
#include "Quests/Types/PrereqLeafSubscription.h"


void UQuestPrereqRuleNode::Activate(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — Rule monitors are utility nodes that do not participate in the quest lifecycle
	// (no Active / Completed / Deactivated state facts). The subscribe + initial-publish work happens in
	// OnRegisteredWithManager (instance lifetime), not here. Activate calls from the compiler's OutMonitorTags
	// entry-tag registration are now harmless redundant pulses — leaving the registration in place preserves
	// backward compatibility; could be dropped as a follow-up cleanup mirroring the Listener migration.
	ContextualTag = InContextualTag;
}

void UQuestPrereqRuleNode::OnRegisteredWithManager()
{
	if (!CachedGameInstance.IsValid()) return;

	// May already be satisfied at graph load (mid-session activation, save-load restore, or any leaf whose
	// fact is already asserted from a prior phase).
	TryPublishRule();

	FPrereqLeafSubscription::SubscribeLeavesForReevaluation(
		Expression,
		this,
		&UQuestPrereqRuleNode::OnLeafFactAdded,
		&UQuestPrereqRuleNode::OnLeafResolutionRecorded,
		&UQuestPrereqRuleNode::OnLeafEntryRecorded,
		SubscriptionHandles);
}

void UQuestPrereqRuleNode::ResetTransientState()
{
	Super::ResetTransientState();
	// SubscriptionHandles point at the prior PIE's SignalSubsystem (now destroyed). Drop them defensively
	// so OnRegisteredWithManager subscribes cleanly via the new session's SignalSubsystem.
	SubscriptionHandles.Reset();
}

void UQuestPrereqRuleNode::OnLeafFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
	TryPublishRule();
}

void UQuestPrereqRuleNode::OnLeafResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event)
{
	TryPublishRule();
}

void UQuestPrereqRuleNode::OnLeafEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event)
{
	TryPublishRule();
}

void UQuestPrereqRuleNode::TryPublishRule()
{
	if (!CachedGameInstance.IsValid()) return;

	UWorldStateSubsystem* WorldState = CachedGameInstance->GetSubsystem<UWorldStateSubsystem>();
	UQuestStateSubsystem* StateSubsystem = CachedGameInstance->GetSubsystem<UQuestStateSubsystem>();
	if (!WorldState || !StateSubsystem) return;
	const bool bIsAlways = Expression.IsAlways();
	const bool bEval = bIsAlways || Expression.Evaluate(WorldState, StateSubsystem);
	const bool bCurrentlyPublished = WorldState->HasFact(GroupTag);

	if (bEval && !bCurrentlyPublished)
	{
		WorldState->AddFact(GroupTag);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("QuestPrereqRule '%s' published (expression %s)"),
			*GroupTag.ToString(), bIsAlways ? TEXT("Always") : TEXT("satisfied"));
	}
	else if (!bEval && bCurrentlyPublished)
	{
		WorldState->RemoveFact(GroupTag);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("QuestPrereqRule '%s' retracted (expression no longer satisfied)"),
			*GroupTag.ToString());
	}

	// Subscriptions stay live for the rule's lifetime: NOT expressions require re-evaluation whenever a leaf
	// transitions. For monotonic AND/OR expressions this is cheap: AddFact on a count-already-positive tag bumps
	// the count without re-broadcasting, and we don't enter the publish branch twice.
}

