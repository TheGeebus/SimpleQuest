// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "WorldState/WorldStateSubsystem.h"
#include "Signals/SignalSubsystem.h"

void UWorldStateSubsystem::SatisfyState(const FGameplayTag StateTag)
{
	if (!StateTag.IsValid() || SatisfiedStateTags.HasTagExact(StateTag)) return;

	SatisfiedStateTags.AddTag(StateTag);

	if (USignalSubsystem* Signals = GetGameInstance()->GetSubsystem<USignalSubsystem>())
	{
		Signals->PublishTyped(this, FStateSatisfiedEvent(StateTag));
	}
}

void UWorldStateSubsystem::UnsatisfyState(const FGameplayTag StateTag)
{
	if (!SatisfiedStateTags.HasTagExact(StateTag)) return;

	SatisfiedStateTags.RemoveTag(StateTag);

	if (USignalSubsystem* Signals = GetGameInstance()->GetSubsystem<USignalSubsystem>())
	{
		Signals->PublishTyped(this, FStateUnsatisfiedEvent(StateTag));
	}
}

bool UWorldStateSubsystem::IsStateSatisfied(const FGameplayTag StateTag) const
{
	return SatisfiedStateTags.HasTagExact(StateTag);
}
