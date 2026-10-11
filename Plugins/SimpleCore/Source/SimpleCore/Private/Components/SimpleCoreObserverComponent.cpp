// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#include "Components/SimpleCoreObserverComponent.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "Subsystems/SignalSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Utilities/SimpleCoreLog.h"


USimpleCoreObserverComponent::USimpleCoreObserverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USimpleCoreObserverComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		SignalSubsystem = GI->GetSubsystem<USignalSubsystem>();
	}
	ensureMsgf(SignalSubsystem, TEXT("%s: the signal bus is unavailable, so nothing on this component will fire."), *GetPathName());

	// Deferred to the next tick so the owning actor finishes its own BeginPlay - creating state, binding these
	// delegates - before anything reaches it. A component's BeginPlay runs AHEAD of its actor's, and registration
	// replays every already-true fact synchronously, so registering here would fire catch-up at a half-built owner.
	// An actor's BeginPlay is synchronous within the frame, so one tick is guaranteed to land after it.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &USimpleCoreObserverComponent::PerformDeferredRegistration);
	}
}

void USimpleCoreObserverComponent::PerformDeferredRegistration()
{
	RegisterFactObserver();
}

void USimpleCoreObserverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Cancel a still-pending deferred registration so it cannot fire onto a torn-down component.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	if (SignalSubsystem)
	{
		SignalSubsystem->UnsubscribeListener(this);
	}
	SubscriptionHandlesByFact.Empty();
	bRegistered = false;
	Super::EndPlay(EndPlayReason);
}

void USimpleCoreObserverComponent::RegisterFactObserver()
{
	if (!GetWorldState())
	{
		UE_LOG(LogSimpleCore, Warning, TEXT("FactObserver::Register: '%s' found no world state subsystem - its %d entries will never fire"),
			*GetPathName(), ObservedFacts.Num());
		return;
	}

	// bRegistered flips BEFORE the empty-set early-out, so a component authored with no entries still takes runtime
	// adds. Starting empty and filling the map from Blueprint is a supported shape, not an oversight.
	bRegistered = true;
	if (ObservedFacts.IsEmpty()) return;

	// Snapshot before iterating. Subscribing replays already-true facts straight into owner handlers, and one of
	// those is free to call AddObservedFact or RemoveObservedFact - which would rehash the map under the loop.
	TArray<TPair<FGameplayTag, FObservedFactSettings>> Entries;
	Entries.Reserve(ObservedFacts.Num());
	for (const TPair<FGameplayTag, FObservedFactSettings>& Entry : ObservedFacts)
	{
		Entries.Add(Entry);
	}

	for (const TPair<FGameplayTag, FObservedFactSettings>& Entry : Entries)
	{
		RegisterSingleObservedFact(Entry.Key, Entry.Value);
	}
	UE_LOG(LogSimpleCore, Verbose, TEXT("FactObserver::Register: '%s' watching %d fact entries"), *GetPathName(), Entries.Num());
}

void USimpleCoreObserverComponent::RegisterSingleObservedFact(const FGameplayTag& FactTag, const FObservedFactSettings& Settings)
{
	UWorldStateSubsystem* WorldState = GetWorldState();
	if (!WorldState) return;
	if (!FactTag.IsValid())
	{
		UE_LOG(LogSimpleCore, Warning, TEXT("FactObserver::Subscribe: '%s' skipped an invalid fact tag"), *GetPathName());
		return;
	}

	// Drop anything already wired for this tag first. Re-registering without it would leave two live subscriptions
	// on one entry and deliver every change twice.
	UnregisterSingleObservedFact(FactTag);

	// Handles are collected locally rather than into the map. Subscribing fires catch-up BEFORE it returns, an owner
	// handler may add or remove entries from inside that replay, and a reference into the map would not survive it.
	TArray<FDelegateHandle> NewHandles;
	if (Settings.bObserveAdded)
	{
		// This call replays every already-true matching fact to THIS component and then wires the live subscription.
		// The replay reaches nobody else, and both halves happen inside the one call, so no publish can land between
		// them and be seen twice.
		NewHandles.Add(WorldState->SubscribeToFactAdded(FactTag, this, &USimpleCoreObserverComponent::HandleFactAdded, Settings.Routing));
	}
	if (Settings.bObserveRemoved)
	{
		NewHandles.Add(WorldState->SubscribeToFactRemoved(FactTag, this, &USimpleCoreObserverComponent::HandleFactRemoved, Settings.Routing));
	}

	if (!NewHandles.IsEmpty())
	{
		SubscriptionHandlesByFact.Add(FactTag, MoveTemp(NewHandles));
	}
	UE_LOG(LogSimpleCore, Verbose, TEXT("FactObserver::Subscribe: '%s' tag='%s' added=%s removed=%s routing=%d"),
		*GetPathName(), *FactTag.ToString(),
		Settings.bObserveAdded ? TEXT("yes") : TEXT("no"),
		Settings.bObserveRemoved ? TEXT("yes") : TEXT("no"),
		static_cast<int32>(Settings.Routing));
}

void USimpleCoreObserverComponent::UnregisterSingleObservedFact(const FGameplayTag& FactTag)
{
	UnsubscribeHandlesForTag(SubscriptionHandlesByFact, FactTag);
}

int32 USimpleCoreObserverComponent::UnsubscribeHandlesForTag(TMap<FGameplayTag, TArray<FDelegateHandle>>& Registry, const FGameplayTag& Tag)
{
	TArray<FDelegateHandle> Handles;
	if (!Registry.RemoveAndCopyValue(Tag, Handles)) return 0;

	if (SignalSubsystem)
	{
		for (const FDelegateHandle& Handle : Handles)
		{
			SignalSubsystem->UnsubscribeMessage(Tag, Handle);
		}
	}
	UE_LOG(LogSimpleCore, Verbose, TEXT("Observer::Unsubscribe: '%s' tag='%s' handles=%d"),
		*GetPathName(), *Tag.ToString(), Handles.Num());
	return Handles.Num();
}

void USimpleCoreObserverComponent::HandleFactAdded(const FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
	// Channel goes unused on purpose. A fact is published on one channel, so for every delivery here - live or
	// replayed - it already equals Event.StateTag. The quest side carries both because a multi-channel publish makes
	// them diverge; a fact publish cannot.
	if (OnFactAdded.IsBound())
	{
		OnFactAdded.Broadcast(Event.StateTag, Event.bCatchUp);
	}
}

void USimpleCoreObserverComponent::HandleFactRemoved(const FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event)
{
	if (OnFactRemoved.IsBound())
	{
		OnFactRemoved.Broadcast(Event.StateTag);
	}
}

void USimpleCoreObserverComponent::AddObservedFact(const FGameplayTag FactTag, const FObservedFactSettings Settings)
{
	if (!FactTag.IsValid())
	{
		UE_LOG(LogSimpleCore, Warning, TEXT("FactObserver::AddObservedFact: '%s' refused an invalid tag"), *GetPathName());
		return;
	}

	ObservedFacts.Add(FactTag, Settings);
	if (bRegistered)
	{
		RegisterSingleObservedFact(FactTag, Settings);
	}
	else
	{
		UE_LOG(LogSimpleCore, Verbose, TEXT("FactObserver::AddObservedFact: '%s' tag='%s' stored - registration has not run yet"),
			*GetPathName(), *FactTag.ToString());
	}
}

void USimpleCoreObserverComponent::RemoveObservedFact(const FGameplayTag FactTag)
{
	if (ObservedFacts.Remove(FactTag) == 0) return;

	if (bRegistered)
	{
		UnregisterSingleObservedFact(FactTag);
	}
}

int32 USimpleCoreObserverComponent::ApplyTagRenames(const TMap<FName, FName>& Renames)
{
	// ObservedFacts has FGameplayTag KEYS, which the editor's reflection sweep cannot address: TMap does not permit
	// in-place key mutation, so the rewrite is remove-then-readd.
	int32 Count = 0;
	for (const TPair<FName, FName>& Rename : Renames)
	{
		FGameplayTag FoundKey;
		for (const TPair<FGameplayTag, FObservedFactSettings>& Entry : ObservedFacts)
		{
			if (Entry.Key.GetTagName() == Rename.Key)
			{
				FoundKey = Entry.Key;
				break;
			}
		}
		if (!FoundKey.IsValid()) continue;

		const FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(Rename.Value, false);
		if (!NewTag.IsValid()) continue;

		FObservedFactSettings Moved = MoveTemp(ObservedFacts[FoundKey]);
		ObservedFacts.Remove(FoundKey);
		ObservedFacts.Add(NewTag, MoveTemp(Moved));
		++Count;
		UE_LOG(LogSimpleCore, Verbose, TEXT("FactObserver::ApplyTagRenames: '%s' '%s' -> '%s'"),
			*GetPathName(), *FoundKey.ToString(), *NewTag.ToString());
	}
	return Count;
}

int32 USimpleCoreObserverComponent::RemoveTags(const TArray<FGameplayTag>& TagsToRemove)
{
	int32 Count = 0;
	for (const FGameplayTag& Tag : TagsToRemove)
	{
		const int32 Removed = ObservedFacts.Remove(Tag);
		if (Removed > 0)
		{
			if (Count == 0) Modify();                  // dirty the owner once, on the first actual removal
			Count += Removed;
			UE_LOG(LogSimpleCore, Verbose, TEXT("FactObserver::RemoveTags: '%s' dropped '%s'"), *GetPathName(), *Tag.ToString());
		}
	}
	return Count;
}

UWorldStateSubsystem* USimpleCoreObserverComponent::GetWorldState() const
{
	const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
}

