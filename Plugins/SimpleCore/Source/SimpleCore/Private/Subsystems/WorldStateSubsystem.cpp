// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Subsystems/WorldStateSubsystem.h"
#include "Subsystems/SignalSubsystem.h"
#include "Utilities/SimpleCoreLog.h"


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
	UE_LOG(LogSimpleCore, Verbose, TEXT("WorldState::AddFact: '%s' count=%d broadcast=%s"),
		*Tag.ToString(),
		NewCount,
		bShouldBroadcast ? TEXT("yes") : TEXT("no"));

	// Inspection-surface broadcast: fires on every AddFact regardless of per-tag broadcast mode (subscribers
	// care about count mutations even when they don't cross the 0/1 boundary).
	OnAnyFactChanged.Broadcast();
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
	UE_LOG(LogSimpleCore, Verbose, TEXT("WorldState::RemoveFact: '%s' remaining=%d broadcast=%s"),
		*Tag.ToString(),
		bReachedZero ? 0 : *Count,
		bShouldBroadcast ? TEXT("yes") : TEXT("no"));

	OnAnyFactChanged.Broadcast();
}

void UWorldStateSubsystem::ClearFact(const FGameplayTag Tag, const bool bSuppressBroadcast)
{
	if (!WorldFacts.Contains(Tag)) return;

	WorldFacts.Remove(Tag);
	UE_LOG(LogSimpleCore, Verbose, TEXT("WorldState::ClearFact: '%s' broadcast=%s"),
		*Tag.ToString(),
		bSuppressBroadcast ? TEXT("no") : TEXT("yes"));

	if (!bSuppressBroadcast)
	{
		if (USignalSubsystem* Signals = GetGameInstance()->GetSubsystem<USignalSubsystem>())
		{
			Signals->PublishMessage(Tag, FWorldStateFactRemovedEvent(Tag));
		}
	}

	OnAnyFactChanged.Broadcast();
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

void UWorldStateSubsystem::GetFactsMatching(const FGameplayTag Channel, const ESignalRoutingMode Routing, TArray<FGameplayTag>& OutFacts) const
{
	OutFacts.Reset();
	if (!Channel.IsValid()) return;

	// Match the bus's own delivery rule so catch-up covers exactly what a live publish would have reached: an exact
	// subscriber hears only its own tag, a descendants subscriber also hears everything beneath it. MatchesTag is
	// true when the fact IS the channel or sits under it.
	const bool bIncludeDescendants = FSignalRoutingDefaults::IncludesDescendants(Routing);
	for (const TPair<FGameplayTag, int32>& Fact : WorldFacts)
	{
		if (Fact.Value <= 0) continue;
		if (bIncludeDescendants ? Fact.Key.MatchesTag(Channel) : Fact.Key == Channel)
		{
			OutFacts.Add(Fact.Key);
		}
	}

	// TMap iteration order is not stable, so an unsorted replay would vary between runs of the same scenario and
	// hide ordering bugs until somebody else found them. Sorting costs nothing at this size and makes catch-up
	// reproducible.
	OutFacts.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.GetTagName().LexicalLess(B.GetTagName()); });

	UE_LOG(LogSimpleCore, Verbose, TEXT("WorldState::GetFactsMatching: channel='%s' routing=%s matched %d fact(s)"),
		*Channel.ToString(),
		bIncludeDescendants ? TEXT("descendants") : TEXT("exact"),
		OutFacts.Num());
}

void UWorldStateSubsystem::RestoreFacts(const TMap<FGameplayTag, int32>& InFacts)
{
	WorldFacts = InFacts;
	OnAnyFactChanged.Broadcast();
}
