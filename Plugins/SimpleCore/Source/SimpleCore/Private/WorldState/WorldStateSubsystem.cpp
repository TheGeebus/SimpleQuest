// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "WorldState/WorldStateSubsystem.h"
#include "Signals/SignalSubsystem.h"

void UWorldStateSubsystem::AddFact(const FGameplayTag StateTag)
{
	if (!StateTag.IsValid() || WorldFacts.HasTagExact(StateTag)) return;

	WorldFacts.AddTag(StateTag);

	if (USignalSubsystem* Signals = GetGameInstance()->GetSubsystem<USignalSubsystem>())
	{
		Signals->PublishTyped(this, FWorldStateFactAddedEvent(StateTag));
	}
}

void UWorldStateSubsystem::RemoveFact(const FGameplayTag StateTag)
{
	if (!WorldFacts.HasTagExact(StateTag)) return;

	WorldFacts.RemoveTag(StateTag);

	if (USignalSubsystem* Signals = GetGameInstance()->GetSubsystem<USignalSubsystem>())
	{
		Signals->PublishTyped(this, FWorldStateFactRemovedEvent(StateTag));
	}
}

bool UWorldStateSubsystem::HasFact(const FGameplayTag StateTag) const
{
	return WorldFacts.HasTagExact(StateTag);
}
