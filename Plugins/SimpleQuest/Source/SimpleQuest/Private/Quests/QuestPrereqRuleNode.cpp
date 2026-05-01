// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestPrereqRuleNode.h"

#include "SimpleQuestLog.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Events/QuestResolutionRecordedEvent.h"

void UQuestPrereqRuleNode::Activate(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — Rule monitors are utility nodes that do not participate in the quest lifecycle
	// (no Active / Completed / Deactivated state facts). They subscribe to Expression leaves and publish the rule tag
	// when the expression evaluates true.
	ContextualTag = InContextualTag;
	if (!CachedGameInstance.IsValid()) return;

	UWorldStateSubsystem* WorldState = CachedGameInstance->GetSubsystem<UWorldStateSubsystem>();
	USignalSubsystem* Signals = CachedGameInstance->GetSubsystem<USignalSubsystem>();
	if (!WorldState || !Signals) return;

	// May already be satisfied at graph load (mid-session activation).
	TryPublishRule();

	// Subscribe per-leaf with type-aware channel: WorldState fact channel for Leaf, resolution-recorded
	// channel for Leaf_Resolution. Either path triggers TryPublishRule via its handler.
	TArray<FPrereqLeafDescriptor> Leaves;
	Expression.CollectLeaves(Leaves);
	for (const FPrereqLeafDescriptor& Leaf : Leaves)
	{
		if (Leaf.Type == EPrerequisiteExpressionType::Leaf)
		{
			const FGameplayTag& LeafTag = Leaf.FactTag;
			if (!LeafTag.IsValid() || SubscriptionHandles.Contains(LeafTag)) continue;
			FDelegateHandle Handle = Signals->SubscribeMessage<FWorldStateFactAddedEvent>(LeafTag, this, &UQuestPrereqRuleNode::OnLeafFactAdded);
			SubscriptionHandles.Add(LeafTag, Handle);
		}
		else if (Leaf.Type == EPrerequisiteExpressionType::Leaf_Resolution)
		{
			const FGameplayTag& LeafQuestTag = Leaf.ResolutionQuestTag;
			if (!LeafQuestTag.IsValid() || SubscriptionHandles.Contains(LeafQuestTag)) continue;
			FDelegateHandle Handle = Signals->SubscribeMessage<FQuestResolutionRecordedEvent>(LeafQuestTag, this, &UQuestPrereqRuleNode::OnLeafResolutionRecorded);
			SubscriptionHandles.Add(LeafQuestTag, Handle);
		}
	}
}

void UQuestPrereqRuleNode::ResetTransientState()
{
	Super::ResetTransientState();
	// SubscriptionHandles referenced the prior PIE's SignalSubsystem. Wipe: if we left them populated, the
	// subscribe loop in Activate would skip every leaf ("already subscribed") and the rule would never hear
	// any leaf-fact-added events this session.
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

