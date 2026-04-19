// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestPrereqRuleNode.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"

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
	if (WorldState->HasFact(GroupTag)) return;

	// Subscribe to every leaf tag of the expression — any arrival triggers re-evaluation.
	TArray<FGameplayTag> LeafTags;
	Expression.CollectLeafTags(LeafTags);
	for (const FGameplayTag& LeafTag : LeafTags)
	{
		if (SubscriptionHandles.Contains(LeafTag)) continue;
		FDelegateHandle Handle = Signals->SubscribeMessage<FWorldStateFactAddedEvent>(LeafTag, this, &UQuestPrereqRuleNode::OnLeafFactAdded);
		SubscriptionHandles.Add(LeafTag, Handle);
	}
}

void UQuestPrereqRuleNode::OnLeafFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
	TryPublishRule();
}

void UQuestPrereqRuleNode::TryPublishRule()
{
	if (!CachedGameInstance.IsValid()) return;

	UWorldStateSubsystem* WorldState = CachedGameInstance->GetSubsystem<UWorldStateSubsystem>();
	if (!WorldState) return;

	if (!Expression.IsAlways() && !Expression.Evaluate(WorldState)) return;

	WorldState->AddFact(GroupTag);

	// Rule is permanently satisfied — unsubscribe.
	if (USignalSubsystem* Signals = CachedGameInstance->GetSubsystem<USignalSubsystem>())
	{
		for (auto& Pair : SubscriptionHandles)
		{
			Signals->UnsubscribeMessage(Pair.Key, Pair.Value);
		}
		SubscriptionHandles.Reset();
	}
}