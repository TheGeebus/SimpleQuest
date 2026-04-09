// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestPrereqGroupNode.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"

void UQuestPrereqGroupNode::Activate(FGameplayTag InContextualTag)
{
	Super::Activate(InContextualTag);
	if (!CachedGameInstance.IsValid()) return;

	UWorldStateSubsystem* WorldState = CachedGameInstance->GetSubsystem<UWorldStateSubsystem>();
	USignalSubsystem* Signals = CachedGameInstance->GetSubsystem<USignalSubsystem>();
	if (!WorldState || !Signals) return;

	// Conditions may already be satisfied if this graph loads mid-session
	TrySatisfyGroup();
	if (WorldState->HasFact(GroupTag)) return;

	for (const FGameplayTag& CondTag : ConditionTags)
	{
		FDelegateHandle Handle = Signals->SubscribeMessage<FWorldStateFactAddedEvent>(CondTag, this, &UQuestPrereqGroupNode::OnConditionFactAdded);
		SubscriptionHandles.Add(CondTag, Handle);
	}
}

void UQuestPrereqGroupNode::OnConditionFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
	TrySatisfyGroup();
}

void UQuestPrereqGroupNode::TrySatisfyGroup()
{
	if (!CachedGameInstance.IsValid()) return;

	UWorldStateSubsystem* WorldState = CachedGameInstance->GetSubsystem<UWorldStateSubsystem>();
	if (!WorldState) return;

	for (const FGameplayTag& CondTag : ConditionTags)
	{
		if (!WorldState->HasFact(CondTag)) return;
	}

	WorldState->AddFact(GroupTag);

	// Group is permanently satisfied — unsubscribe everything
	if (USignalSubsystem* Signals = CachedGameInstance->GetSubsystem<USignalSubsystem>())
	{
		for (auto& Pair : SubscriptionHandles)
		{
			Signals->UnsubscribeMessage(Pair.Key, Pair.Value);
		}
		SubscriptionHandles.Reset();
	}
}
