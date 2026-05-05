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

		// Catch-up: fire delegates immediately for state already present at subscription time. Synthetic Context
		// carries just QuestTag — full Context isn't recoverable from state alone (NodeInfo, CompletionContext,
		// GameData come from the runtime publish-time AssembleEventContext call). Mirrors UQuestEventSubscription's
		// synthetic-context pattern.
		FQuestEventContext SyntheticContext;
		SyntheticContext.NodeInfo.QuestTag = QuestTag;

		// PendingGiver fact gates both Activated and Enabled catch-up.
		const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(
			FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::PendingGiver), false);
		const bool bIsPendingGiver = PendingFact.IsValid() && WorldState->HasFact(PendingFact);

		FQuestPrereqStatus CachedPrereqStatus;
		if (bIsPendingGiver && StateSubsystem)
		{
			CachedPrereqStatus = StateSubsystem->GetQuestPrereqStatus(QuestTag);
		}

		if (Settings.bWatchActivated && bIsPendingGiver)
		{
			ActiveQuestTags.AddTag(QuestTag);
			if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(QuestTag, SyntheticContext, CachedPrereqStatus);
		}

		if (Settings.bWatchEnabled && bIsPendingGiver && CachedPrereqStatus.bSatisfied)
		{
			ActiveQuestTags.AddTag(QuestTag);
			if (OnQuestEnabled.IsBound()) OnQuestEnabled.Broadcast(QuestTag, SyntheticContext);
		}

		if (Settings.bWatchStarted)
		{
			const FGameplayTag LiveFact = UGameplayTagsManager::Get().RequestGameplayTag(
				FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Live), false);
			if (LiveFact.IsValid() && WorldState->HasFact(LiveFact))
			{
				ActiveQuestTags.AddTag(QuestTag);
				// Catch-up GiverActor is null — RecentGiverActors was consumed at start time and isn't recovered.
				if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(QuestTag, SyntheticContext, nullptr);
			}
		}

		// Disabled / GiveBlocked / Progress / Unblocked have no catch-up — transient or one-shot events without
		// recoverable state.

		if (Settings.bWatchCompleted)
		{
			const FGameplayTag CompletedFact = UGameplayTagsManager::Get().RequestGameplayTag(
				FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Completed), false);
			if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact))
			{
				ActiveQuestTags.RemoveTag(QuestTag);
				CompletedQuestTags.AddTag(QuestTag);

				FGameplayTag RecoveredOutcome = FGameplayTag::EmptyTag;
				if (StateSubsystem)
				{
					if (const FQuestResolutionRecord* Record = StateSubsystem->GetQuestResolution(QuestTag))
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
						*QuestTag.ToString(), *RecoveredOutcome.ToString());
				}
				else
				{
					UE_LOG(LogSimpleQuest, Log, TEXT("QuestWatcher: catch-up for '%s' — recovered outcome '%s' from registry"),
						*QuestTag.ToString(), *RecoveredOutcome.ToString());
					if (OnQuestCompleted.IsBound()) OnQuestCompleted.Broadcast(QuestTag, RecoveredOutcome, SyntheticContext);
				}
			}
		}

		if (Settings.bWatchDeactivated)
		{
			const FGameplayTag DeactivatedFact = UGameplayTagsManager::Get().RequestGameplayTag(
				FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Deactivated), false);
			if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
			{
				ActiveQuestTags.RemoveTag(QuestTag);
				if (OnQuestDeactivated.IsBound()) OnQuestDeactivated.Broadcast(QuestTag, SyntheticContext);
			}
		}

		if (Settings.bWatchBlocked)
		{
			const FGameplayTag BlockedFact = UGameplayTagsManager::Get().RequestGameplayTag(
				FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Blocked), false);
			if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
			{
				if (OnQuestBlocked.IsBound()) OnQuestBlocked.Broadcast(QuestTag, SyntheticContext);
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