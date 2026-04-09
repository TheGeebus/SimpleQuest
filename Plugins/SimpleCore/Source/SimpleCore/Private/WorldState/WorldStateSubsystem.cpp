// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "WorldState/WorldStateSubsystem.h"
#include "Signals/SignalSubsystem.h"

void UWorldStateSubsystem::AddFact(const FGameplayTag Tag, const EFactBroadcastMode BroadcastMode)
{
	if (!Tag.IsValid()) return;

	const int32 NewCount = ++WorldFacts.FindOrAdd(Tag);
	const bool bShouldBroadcast = BroadcastMode == EFactBroadcastMode::Always || (BroadcastMode == EFactBroadcastMode::BoundaryOnly && NewCount == 1);
	if (bShouldBroadcast)
	{
		if (USignalSubsystem* Signals = GetGameInstance()->GetSubsystem<USignalSubsystem>())
		{
			Signals->PublishMessage(Tag, FWorldStateFactAddedEvent(Tag));
		}
	}
}

void UWorldStateSubsystem::RemoveFact(const FGameplayTag Tag, const EFactBroadcastMode BroadcastMode)
{
	int32* Count = WorldFacts.Find(Tag);
	if (!Count || *Count <= 0) return;

	const bool bReachedZero = (--(*Count) == 0);
	if (bReachedZero) WorldFacts.Remove(Tag);

	const bool bShouldBroadcast = BroadcastMode == EFactBroadcastMode::Always || (BroadcastMode == EFactBroadcastMode::BoundaryOnly && bReachedZero);
	if (bShouldBroadcast)
	{
		if (USignalSubsystem* Signals = GetGameInstance()->GetSubsystem<USignalSubsystem>())
		{
			Signals->PublishMessage(Tag, FWorldStateFactRemovedEvent(Tag));
		}
	}
}

void UWorldStateSubsystem::ClearFact(const FGameplayTag Tag, const bool bSuppressBroadcast)
{
	if (!WorldFacts.Contains(Tag)) return;

	WorldFacts.Remove(Tag);
	if (!bSuppressBroadcast)
	{
		if (USignalSubsystem* Signals = GetGameInstance()->GetSubsystem<USignalSubsystem>())
		{
			Signals->PublishMessage(Tag, FWorldStateFactRemovedEvent(Tag));
		}
	}
}

bool UWorldStateSubsystem::HasFact(const FGameplayTag Tag) const
{
	const int32* Count = WorldFacts.Find(Tag);
	return Count && *Count > 0;
}

int32 UWorldStateSubsystem::GetFactValue(const FGameplayTag Tag) const
{
	const int32* Count = WorldFacts.Find(Tag);
	return Count ? *Count : 0;
}