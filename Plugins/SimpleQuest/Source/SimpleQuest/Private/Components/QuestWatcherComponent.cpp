// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestWatcherComponent.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"


UQuestWatcherComponent::UQuestWatcherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestWatcherComponent::BeginPlay()
{
	Super::BeginPlay();

	RegisterQuestWatcher();
}

void UQuestWatcherComponent::WatchedQuestActivatedEvent(FGameplayTag Channel, const FQuestEnabledEvent& QuestEnabledEvent)
{
	ActiveQuestTags.AddTag(QuestEnabledEvent.GetQuestTag());
	if (OnQuestActivated.IsBound())
	{
		OnQuestActivated.Broadcast(QuestEnabledEvent.GetQuestTag());
	}
}

void UQuestWatcherComponent::WatchedQuestStartedEvent(FGameplayTag Channel, const FQuestStartedEvent& QuestStartedEvent)
{
	if (OnQuestStarted.IsBound())
	{
		OnQuestStarted.Broadcast(QuestStartedEvent.GetQuestTag());
	}
}

void UQuestWatcherComponent::WatchedQuestCompletedEvent(FGameplayTag Channel, const FQuestEndedEvent& QuestEndedEvent)
{
	ActiveQuestTags.RemoveTag(QuestEndedEvent.GetQuestTag());
	CompletedQuestTags.AddTag(QuestEndedEvent.GetQuestTag());

	// Apply outcome filter — if the settings specify outcomes, only broadcast for matches
	if (const FWatchedQuestEventSettings* Settings = WatchedTags.Find(Channel))
	{
		if (!Settings->OutcomeFilter.IsEmpty() && !Settings->OutcomeFilter.HasTagExact(QuestEndedEvent.OutcomeTag))
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("QuestWatcher: quest '%s' completed with outcome '%s' — filtered out, skipping broadcast"),
				*QuestEndedEvent.GetQuestTag().ToString(),
				*QuestEndedEvent.OutcomeTag.ToString());
			return;
		}
	}

	if (OnQuestCompleted.IsBound())
	{
		OnQuestCompleted.Broadcast(QuestEndedEvent.GetQuestTag(), QuestEndedEvent.OutcomeTag);
	}
}

void UQuestWatcherComponent::WatchedQuestDeactivatedEvent(FGameplayTag Channel, const FQuestDeactivatedEvent& QuestDeactivatedEvent)
{
	ActiveQuestTags.RemoveTag(QuestDeactivatedEvent.GetQuestTag());
	if (OnQuestDeactivated.IsBound())
	{
		OnQuestDeactivated.Broadcast(QuestDeactivatedEvent.GetQuestTag());
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
	UQuestStateSubsystem* ResolutionRegistry = GameInstance ? GameInstance->GetSubsystem<UQuestStateSubsystem>() : nullptr;

	for (auto& QuestPair : WatchedTags)
	{
		const FGameplayTag& QuestTag = QuestPair.Key;
		if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
		{
			UE_LOG(LogSimpleQuest, Warning,
				TEXT("UQuestWatcherComponent::RegisterQuestWatcher : '%s' holds stale tag '%s' — skipping subscribe. ")
				TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
				*GetOwner()->GetActorNameOrLabel(), *QuestTag.ToString());
			continue;
		}

        UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : Registered watcher for tag: %s"), *QuestTag.ToString());

        if (QuestPair.Value.bWatchActivation) SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestActivatedEvent);
        if (QuestPair.Value.bWatchStart) SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestStartedEvent);
        if (QuestPair.Value.bWatchEnd)	SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestCompletedEvent);
    	if (QuestPair.Value.bWatchDeactivation) SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestDeactivatedEvent);

        if (!WorldState) continue;

        // Catch-up: fire delegates immediately for facts already written before this component registered.
        // Step start/complete are transient and not caught up — only persistent quest-level state is covered.

        // Quest is waiting for a giver
        if (QuestPair.Value.bWatchActivation)
        {
            const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::PendingGiver), false);
            if (WorldState->HasFact(PendingFact))
            {
                ActiveQuestTags.AddTag(QuestTag);
                if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(QuestTag);
            }
        }

        // Quest is currently active
        if (QuestPair.Value.bWatchStart)
        {
            const FGameplayTag LiveFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Live), false);
            if (WorldState->HasFact(LiveFact))
            {
                ActiveQuestTags.AddTag(QuestTag);
                if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(QuestTag);
            }
        }

		// Quest has already completed — recover the actual outcome from the resolution subsystem instead of probing WorldState.
		// The boolean Completed fact is still the gating "did this resolve at all" check (fast O(1) layer); the registry then
		// answers "with what outcome" (rich-record layer). Together they remove the prior EmptyTag fallback and per-filter-tag
		// probe loop in favor of a single keyed lookup that always recovers the real OutcomeTag.
		if (QuestPair.Value.bWatchEnd)
		{
			const FGameplayTag CompletedFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Completed), false);
			if (WorldState->HasFact(CompletedFact))
			{
				ActiveQuestTags.RemoveTag(QuestTag);
				CompletedQuestTags.AddTag(QuestTag);

				FGameplayTag RecoveredOutcome;
				if (ResolutionRegistry)
				{
					if (const FQuestResolutionRecord* Record = ResolutionRegistry->GetQuestResolution(QuestTag))
					{
						if (const FQuestResolutionEntry* Latest = Record->GetLatest())
						{
							RecoveredOutcome = Latest->OutcomeTag;
						}
					}
				}

				// Apply the post-hoc OutcomeFilter — same semantics as the live WatchedQuestCompletedEvent path.
				if (!QuestPair.Value.OutcomeFilter.IsEmpty() && !QuestPair.Value.OutcomeFilter.HasTagExact(RecoveredOutcome))
				{
					UE_LOG(LogSimpleQuest, Verbose, TEXT("QuestWatcher: catch-up for '%s' recovered outcome '%s' — filtered out, skipping broadcast"),
						*QuestTag.ToString(), *RecoveredOutcome.ToString());
				}
				else
				{
					UE_LOG(LogSimpleQuest, Log, TEXT("QuestWatcher: catch-up for '%s' — recovered outcome '%s' from registry"),
						*QuestTag.ToString(), *RecoveredOutcome.ToString());
					if (OnQuestCompleted.IsBound()) OnQuestCompleted.Broadcast(QuestTag, RecoveredOutcome);
				}
			}
		}
    	
    	// Quest has been deactivated
    	if (QuestPair.Value.bWatchDeactivation)
    	{
    		const FGameplayTag DeactivatedFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Deactivated), false);
    		if (WorldState->HasFact(DeactivatedFact))
    		{
    			ActiveQuestTags.RemoveTag(QuestTag);
    			if (OnQuestDeactivated.IsBound()) OnQuestDeactivated.Broadcast(QuestTag);
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

