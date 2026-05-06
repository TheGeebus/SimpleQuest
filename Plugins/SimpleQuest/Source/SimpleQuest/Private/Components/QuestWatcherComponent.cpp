// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestWatcherComponent.h"

#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestBlockedEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestDisabledEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestProgressEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestUnblockedEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Utilities/QuestCatchUpFanout.h"
#include "Utilities/QuestTagComposer.h"
#include "WorldState/WorldStateSubsystem.h"


UQuestWatcherComponent::UQuestWatcherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestWatcherComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterQuestWatcher();
}

void UQuestWatcherComponent::HandleQuestActivated(FGameplayTag Channel, const FQuestActivatedEvent& Event)
{
	if (OnQuestActivated.IsBound())
	{
		OnQuestActivated.Broadcast(Event.GetQuestTag(), Event.Context, Event.PrereqStatus);
	}
}

void UQuestWatcherComponent::HandleQuestEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
	ActiveQuestTags.AddTag(Event.GetQuestTag());
	if (OnQuestEnabled.IsBound())
	{
		OnQuestEnabled.Broadcast(Event.GetQuestTag(), Event.Context);
	}
}

void UQuestWatcherComponent::HandleQuestDisabled(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
	if (OnQuestDisabled.IsBound())
	{
		OnQuestDisabled.Broadcast(Event.GetQuestTag(), Event.Context);
	}
}

void UQuestWatcherComponent::HandleQuestGiveBlocked(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
	if (OnQuestGiveBlocked.IsBound())
	{
		OnQuestGiveBlocked.Broadcast(Event.GetQuestTag(), Event.Blockers, Event.GiverActor.Get());
	}
}

void UQuestWatcherComponent::HandleQuestStarted(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
	if (OnQuestStarted.IsBound())
	{
		OnQuestStarted.Broadcast(Event.GetQuestTag(), Event.Context, Event.GiverActor.Get());
	}
}

void UQuestWatcherComponent::HandleQuestProgress(FGameplayTag Channel, const FQuestProgressEvent& Event)
{
	if (OnQuestProgress.IsBound())
	{
		OnQuestProgress.Broadcast(Event.GetQuestTag(), Event.Context);
	}
}

void UQuestWatcherComponent::HandleQuestCompleted(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
	ActiveQuestTags.RemoveTag(Event.GetQuestTag());
	CompletedQuestTags.AddTag(Event.GetQuestTag());

	// Apply outcome filter — if the settings specify outcomes, only broadcast for matches
	if (const FWatchedQuestEventSettings* Settings = WatchedTags.Find(Channel))
	{
		if (!Settings->OutcomeFilter.IsEmpty() && !Settings->OutcomeFilter.HasTagExact(Event.OutcomeTag))
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("QuestWatcher: quest '%s' completed with outcome '%s' — filtered out, skipping broadcast"),
				*Event.GetQuestTag().ToString(),
				*Event.OutcomeTag.ToString());
			return;
		}
	}

	if (OnQuestCompleted.IsBound())
	{
		OnQuestCompleted.Broadcast(Event.GetQuestTag(), Event.OutcomeTag, Event.Context);
	}
}

void UQuestWatcherComponent::HandleQuestDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
	ActiveQuestTags.RemoveTag(Event.GetQuestTag());
	if (OnQuestDeactivated.IsBound())
	{
		OnQuestDeactivated.Broadcast(Event.GetQuestTag(), Event.Context);
	}
}

void UQuestWatcherComponent::HandleQuestBlocked(FGameplayTag Channel, const FQuestBlockedEvent& Event)
{
	if (OnQuestBlocked.IsBound())
	{
		OnQuestBlocked.Broadcast(Event.GetQuestTag(), Event.Context);
	}
}

void UQuestWatcherComponent::HandleQuestUnblocked(FGameplayTag Channel, const FQuestUnblockedEvent& Event)
{
	if (OnQuestUnblocked.IsBound())
	{
		OnQuestUnblocked.Broadcast(Event.GetQuestTag(), Event.Context);
	}
}

int32 UQuestWatcherComponent::ApplyTagRenames(const TMap<FName, FName>& Renames)
{
	int32 Count = 0;
	for (const auto& [OldName, NewName] : Renames)
	{
		// WatchedStepTags
		FGameplayTag FoundOld;
		for (const FGameplayTag& Tag : WatchedStepTags.GetGameplayTagArray())
		{
			if (Tag.GetTagName() == OldName)
			{
				FoundOld = Tag;
				break;
			}
		}
		if (FoundOld.IsValid())
		{
			WatchedStepTags.RemoveTag(FoundOld);
			FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(NewName, false);
			if (NewTag.IsValid())
			{
				WatchedStepTags.AddTag(NewTag);
			}
			Count++;
		}

		// WatchedTags TMap keys — find old tag among keys
		FGameplayTag FoundMapKey;
		for (const auto& [Key, Value] : WatchedTags)
		{
			if (Key.GetTagName() == OldName)
			{
				FoundMapKey = Key;
				break;
			}
		}
		if (FoundMapKey.IsValid())
		{
			FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(NewName, false);
			if (NewTag.IsValid())
			{
				FWatchedQuestEventSettings Moved = MoveTemp(WatchedTags[FoundMapKey]);
				WatchedTags.Remove(FoundMapKey);
				WatchedTags.Add(NewTag, MoveTemp(Moved));
				Count++;
			}
		}
	}
	return Count;
}

int32 UQuestWatcherComponent::RemoveTags(const TArray<FGameplayTag>& TagsToRemove)
{
	int32 Count = 0;
	for (const FGameplayTag& Tag : TagsToRemove)
	{
		if (WatchedStepTags.HasTagExact(Tag))
		{
			if (Count == 0) Modify();
			WatchedStepTags.RemoveTag(Tag);
			++Count;
		}
		const int32 MapRemoved = WatchedTags.Remove(Tag);
		if (MapRemoved > 0)
		{
			if (Count == 0) Modify();
			Count += MapRemoved;
		}
	}
	if (Count > 0 && GetOwner())
	{
		GetOwner()->MarkPackageDirty();
	}
	return Count;
}

void UQuestWatcherComponent::RegisterQuestWatcher()
{
	if (!SignalSubsystem)
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : QuestSignalSubsystem is null, aborting."));
		return;
	}
	if (WatchedTags.IsEmpty())
	{
		if (GetOwner())
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : WatchedQuests is empty. Actor: %s"), *GetOwner()->GetActorNameOrLabel());
		}
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UQuestWatcherComponent_RegisterQuestWatcher);

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UWorldStateSubsystem* WorldState = GameInstance ? GameInstance->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	UQuestStateSubsystem* StateSubsystem = GameInstance ? GameInstance->GetSubsystem<UQuestStateSubsystem>() : nullptr;

	for (auto& QuestPair : WatchedTags)
	{
		const FGameplayTag& QuestTag = QuestPair.Key;
		const FWatchedQuestEventSettings& Settings = QuestPair.Value;

		if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
		{
			UE_LOG(LogSimpleQuest, Warning,
				TEXT("UQuestWatcherComponent::RegisterQuestWatcher : '%s' holds stale tag '%s' — skipping subscribe. ")
				TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
				*GetOwner()->GetActorNameOrLabel(), *QuestTag.ToString());
			continue;
		}

		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : Registered watcher for tag: %s"), *QuestTag.ToString());

		// Live subscriptions — one per opted-in event type. Blocked/Unblocked subscribe directly to their own
		// dedicated events (no piggybacking on FQuestDeactivatedEvent).
		if (Settings.bWatchActivated)    SignalSubsystem->SubscribeMessage<FQuestActivatedEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestActivated);
		if (Settings.bWatchEnabled)      SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestEnabled);
		if (Settings.bWatchDisabled)     SignalSubsystem->SubscribeMessage<FQuestDisabledEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestDisabled);
		if (Settings.bWatchGiveBlocked)  SignalSubsystem->SubscribeMessage<FQuestGiveBlockedEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestGiveBlocked);
		if (Settings.bWatchStarted)      SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestStarted);
		if (Settings.bWatchProgress)     SignalSubsystem->SubscribeMessage<FQuestProgressEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestProgress);
		if (Settings.bWatchCompleted)    SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestCompleted);
		if (Settings.bWatchDeactivated)  SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestDeactivated);
		if (Settings.bWatchBlocked)      SignalSubsystem->SubscribeMessage<FQuestBlockedEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestBlocked);
		if (Settings.bWatchUnblocked)    SignalSubsystem->SubscribeMessage<FQuestUnblockedEvent>(QuestTag, this, &UQuestWatcherComponent::HandleQuestUnblocked);

		if (!WorldState) continue;

		// Catch-up: fire delegates immediately for state already present at subscription time. For an exact-tag
		// watcher this fires per-pin if a matching state fact is set for QuestTag itself; for a parent-prefix
		// watcher (subscribed tag is an unknown namespace OR a known wrapper container) it fans out to every
		// known descendant via FQuestCatchUpFanout and probes each one in turn, mirroring the signal bus's
		// hierarchical broadcast on the live side. Synthetic Context carries each descendant's tag — full Context
		// isn't recoverable from state alone (NodeInfo, CompletionContext, GameData come from the runtime publish-
		// time AssembleEventContext call). Mirrors UQuestEventSubscription's catch-up pattern.
		//
		// No per-tag dedup against live events here (unlike UQuestEventSubscription): the watcher's catch-up runs
		// synchronously inside RegisterQuestWatcher (called from BeginPlay), so there's no deferral window during
		// which a live event could fire and need dedup. The K2 node defers to next tick to avoid racing the BP
		// execution stack; the watcher has no such constraint.
		const TArray<FGameplayTag> CatchUpTags = FQuestCatchUpFanout::EnumerateTagsForCatchUp(QuestTag, StateSubsystem);

		for (const FGameplayTag& EachTag : CatchUpTags)
		{
			FQuestEventContext SyntheticContext;
			SyntheticContext.NodeInfo.QuestTag = EachTag;

			// PendingGiver fact gates both Activated and Enabled catch-up.
			const FGameplayTag PendingFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::PendingGiver);
			const bool bIsPendingGiver = PendingFact.IsValid() && WorldState->HasFact(PendingFact);

			FQuestPrereqStatus CachedPrereqStatus;
			if (bIsPendingGiver && StateSubsystem)
			{
				CachedPrereqStatus = StateSubsystem->GetQuestPrereqStatus(EachTag);
			}

			if (Settings.bWatchActivated && bIsPendingGiver)
			{
				ActiveQuestTags.AddTag(EachTag);
				if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(EachTag, SyntheticContext, CachedPrereqStatus);
			}

			if (Settings.bWatchEnabled && bIsPendingGiver && CachedPrereqStatus.bSatisfied)
			{
				ActiveQuestTags.AddTag(EachTag);
				if (OnQuestEnabled.IsBound()) OnQuestEnabled.Broadcast(EachTag, SyntheticContext);
			}

			if (Settings.bWatchStarted)
			{
				const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Live);
				if (LiveFact.IsValid() && WorldState->HasFact(LiveFact))
				{
					ActiveQuestTags.AddTag(EachTag);
					// GiverActor recovered from the registry's per-tag historical context (FQuestEntryArrival's
					// ActivationParamsSnapshot.ActivationSource captured at start time and preserved past consumption).
					// Pre-§1.2 this catch-up fired with nullptr because the manager's RecentGiverActors map was a
					// single-shot consume-at-broadcast record; now the registry retains the giver attribution.
					AActor* RecoveredGiver = StateSubsystem ? StateSubsystem->GetLastGiverActor(EachTag) : nullptr;
					if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(EachTag, SyntheticContext, RecoveredGiver);
				}
			}

			// Disabled / GiveBlocked / Progress / Unblocked have no catch-up — transient or one-shot events without
			// recoverable state.

			if (Settings.bWatchCompleted)
			{
				const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Completed);
				if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact))
				{
					ActiveQuestTags.RemoveTag(EachTag);
					CompletedQuestTags.AddTag(EachTag);

					FGameplayTag RecoveredOutcome = FGameplayTag::EmptyTag;
					if (StateSubsystem)
					{
						if (const FQuestResolutionRecord* Record = StateSubsystem->GetQuestResolution(EachTag))
						{
							if (const FQuestResolutionEntry* Latest = Record->GetLatest())
							{
								RecoveredOutcome = Latest->OutcomeTag;
							}
						}
					}

					if (!Settings.OutcomeFilter.IsEmpty() && !Settings.OutcomeFilter.HasTagExact(RecoveredOutcome))
					{
						UE_LOG(LogSimpleQuest, Verbose, TEXT("QuestWatcher: catch-up for '%s' recovered outcome '%s' — filtered out, skipping broadcast"),
							*EachTag.ToString(), *RecoveredOutcome.ToString());
					}
					else
					{
						UE_LOG(LogSimpleQuest, Log, TEXT("QuestWatcher: catch-up for '%s' — recovered outcome '%s' from registry"),
							*EachTag.ToString(), *RecoveredOutcome.ToString());
						if (OnQuestCompleted.IsBound()) OnQuestCompleted.Broadcast(EachTag, RecoveredOutcome, SyntheticContext);
					}
				}
			}

			if (Settings.bWatchDeactivated)
			{
				const FGameplayTag DeactivatedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Deactivated);
				if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
				{
					ActiveQuestTags.RemoveTag(EachTag);
					if (OnQuestDeactivated.IsBound()) OnQuestDeactivated.Broadcast(EachTag, SyntheticContext);
				}
			}

			if (Settings.bWatchBlocked)
			{
				const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Blocked);
				if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
				{
					if (OnQuestBlocked.IsBound()) OnQuestBlocked.Broadcast(EachTag, SyntheticContext);
				}
			}
		}
	}
}

FGameplayTagContainer UQuestWatcherComponent::GetRegisteredWatchedStepTags() const
{
	return FQuestTagComposer::FilterToRegisteredTags(
		WatchedStepTags,
		FString::Printf(TEXT("UQuestWatcherComponent::GetRegisteredWatchedStepTags ('%s')"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

FGameplayTagContainer UQuestWatcherComponent::GetRegisteredWatchedQuestKeys() const
{
	FGameplayTagContainer KeysContainer;
	for (const auto& Pair : WatchedTags) KeysContainer.AddTag(Pair.Key);
	return FQuestTagComposer::FilterToRegisteredTags(
		KeysContainer,
		FString::Printf(TEXT("UQuestWatcherComponent::GetRegisteredWatchedQuestKeys ('%s')"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}