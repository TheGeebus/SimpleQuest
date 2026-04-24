// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestWatcherComponent.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Signals/SignalSubsystem.h"


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
			WatchedStepTags.RemoveTag(Tag);
			++Count;
		}
		if (WatchedTags.Remove(Tag) > 0)
		{
			++Count;
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

    UWorldStateSubsystem* WorldState = GetWorld() && GetWorld()->GetGameInstance()
        ? GetWorld()->GetGameInstance()->GetSubsystem<UWorldStateSubsystem>() : nullptr;

	for (auto& QuestPair : WatchedTags)
	{
		const FGameplayTag& QuestTag = QuestPair.Key;
		if (!FQuestStateTagUtils::IsTagRegisteredInRuntime(QuestTag))
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
            const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver), false);
            if (WorldState->HasFact(PendingFact))
            {
                ActiveQuestTags.AddTag(QuestTag);
                if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(QuestTag);
            }
        }

        // Quest is currently active
        if (QuestPair.Value.bWatchStart)
        {
            const FGameplayTag ActiveFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Active), false);
            if (WorldState->HasFact(ActiveFact))
            {
                ActiveQuestTags.AddTag(QuestTag);
                if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(QuestTag);
            }
        }

    	// Quest has already completed
    	if (QuestPair.Value.bWatchEnd)
    	{
    		const FGameplayTag CompletedFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Completed), false);
    		if (WorldState->HasFact(CompletedFact))
    		{
    			ActiveQuestTags.RemoveTag(QuestTag);
    			CompletedQuestTags.AddTag(QuestTag);

    			if (!QuestPair.Value.OutcomeFilter.IsEmpty())
    			{
    				// Outcome filter set — probe WorldState for each filtered outcome fact. MakeNodeOutcomeFact constructs
    				// Quest.State.<Path>.Outcome.<Leaf>, which SetQuestResolved writes on completion.
    				for (const FGameplayTag& OutcomeTag : QuestPair.Value.OutcomeFilter.GetGameplayTagArray())
    				{
    					const FName OutcomeFactName = FQuestStateTagUtils::MakeNodeOutcomeFact(QuestTag.GetTagName(), OutcomeTag);
    					const FGameplayTag OutcomeFact = UGameplayTagsManager::Get().RequestGameplayTag(OutcomeFactName, false);
    					if (OutcomeFact.IsValid() && WorldState->HasFact(OutcomeFact))
    					{
    						UE_LOG(LogSimpleQuest, Log, TEXT("QuestWatcher: catch-up for '%s' — matched outcome '%s' from WorldState"),
								*QuestTag.ToString(), *OutcomeTag.ToString());
    						if (OnQuestCompleted.IsBound()) OnQuestCompleted.Broadcast(QuestTag, OutcomeTag);
    						break; // A quest resolves with exactly one outcome
    					}
    				}
    				// If no filter tag matched, the quest completed with an outcome this watcher doesn't care about. Skip
    				// broadcast (correct filtered behavior).
    			}
    			else
    			{
    				// No filter — outcome fact exists in WorldState but we can't enumerate without FindFactsUnderTag.
    				// Broadcast with EmptyTag (known limitation).
    				if (OnQuestCompleted.IsBound()) OnQuestCompleted.Broadcast(QuestTag, FGameplayTag::EmptyTag);
    			}
    		}
    	}
    	
    	// Quest has been deactivated
    	if (QuestPair.Value.bWatchDeactivation)
    	{
    		const FGameplayTag DeactivatedFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Deactivated), false);
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
	return FQuestStateTagUtils::FilterToRegisteredTags(
		WatchedStepTags,
		FString::Printf(TEXT("UQuestWatcherComponent::GetRegisteredWatchedStepTags ('%s')"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

FGameplayTagContainer UQuestWatcherComponent::GetRegisteredWatchedQuestKeys() const
{
	FGameplayTagContainer KeysContainer;
	for (const auto& Pair : WatchedTags) KeysContainer.AddTag(Pair.Key);
	return FQuestStateTagUtils::FilterToRegisteredTags(
		KeysContainer,
		FString::Printf(TEXT("UQuestWatcherComponent::GetRegisteredWatchedQuestKeys ('%s')"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

