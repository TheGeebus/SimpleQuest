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
	if (!QuestEnabledEvent.bIsActivated) { return; }
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
        if (!QuestTag.IsValid()) continue;

        UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : Registered watcher for tag: %s"), *QuestTag.ToString());

        if (QuestPair.Value.bWatchQuestEnabled) SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestActivatedEvent);
        if (QuestPair.Value.bWatchQuestStart) SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestStartedEvent);
        if (QuestPair.Value.bWatchQuestEnd)	SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestCompletedEvent);
    	if (QuestPair.Value.bWatchDeactivated) SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestDeactivatedEvent);

        if (!WorldState) continue;

        // Catch-up: fire delegates immediately for facts already written before this component registered.
        // Step start/complete are transient and not caught up — only persistent quest-level state is covered.

        // Quest is waiting for a giver
        if (QuestPair.Value.bWatchQuestEnabled)
        {
            const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_PendingGiver), false);
            if (WorldState->HasFact(PendingFact))
            {
                ActiveQuestTags.AddTag(QuestTag);
                if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(QuestTag);
            }
        }

        // Quest is currently active
        if (QuestPair.Value.bWatchQuestStart)
        {
            const FGameplayTag ActiveFact = UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Active), false);
            if (WorldState->HasFact(ActiveFact))
            {
                ActiveQuestTags.AddTag(QuestTag);
                if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(QuestTag);
            }
        }

        // Quest has already completed — OutcomeTag unavailable at catch-up time without additional WorldState enumeration;
        // broadcast with EmptyTag as a known limitation.
        if (QuestPair.Value.bWatchQuestEnd)
        {
            const FGameplayTag CompletedFact = UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Completed), false);
            if (WorldState->HasFact(CompletedFact))
            {
                ActiveQuestTags.RemoveTag(QuestTag);
                CompletedQuestTags.AddTag(QuestTag);
                if (OnQuestCompleted.IsBound()) OnQuestCompleted.Broadcast(QuestTag, FGameplayTag::EmptyTag);
            }
        }
    	
    	// Quest has been deactivated
    	if (QuestPair.Value.bWatchDeactivated)
    	{
    		const FGameplayTag DeactivatedFact = UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Deactivated), false);
    		if (WorldState->HasFact(DeactivatedFact))
    		{
    			ActiveQuestTags.RemoveTag(QuestTag);
    			if (OnQuestDeactivated.IsBound()) OnQuestDeactivated.Broadcast(QuestTag);
    		}
    	}
    }
}


