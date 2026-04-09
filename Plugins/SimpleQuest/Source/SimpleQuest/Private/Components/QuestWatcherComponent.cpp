// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestWatcherComponent.h"

#include "SimpleQuestLog.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestStepCompletedEvent.h"
#include "Events/QuestStepStartedEvent.h"
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

void UQuestWatcherComponent::WatchedQuestStepStartedEvent(FGameplayTag Channel, const FQuestStepStartedEvent& QuestStepStartedEvent)
{
	if (OnQuestStepStarted.IsBound())
	{
		OnQuestStepStarted.Broadcast(QuestStepStartedEvent.GetQuestTag());
	}
}

void UQuestWatcherComponent::WatchedQuestStepCompletedEvent(FGameplayTag Channel, const FQuestStepCompletedEvent& QuestStepCompletedEvent)
{
	if (OnQuestStepCompleted.IsBound())
	{
		OnQuestStepCompleted.Broadcast(QuestStepCompletedEvent.GetQuestTag(), QuestStepCompletedEvent.bDidSucceed, QuestStepCompletedEvent.bEndedQuest);
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

void UQuestWatcherComponent::RegisterQuestWatcher()
{
	if (!CheckQuestSignalSubsystem())
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : QuestSignalSubsystem is null, aborting."));
		return;
	}
	if (!WatchedQuests.IsEmpty())
	{
		for (auto& QuestPair : WatchedQuests)
		{
			const FGameplayTag& QuestTag = QuestPair.Key;
			if (!QuestTag.IsValid()) { continue; }

			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : Registered watcher for tag: %s"), *QuestTag.ToString());

			if (QuestPair.Value.bWatchQuestEnabled)
			{
				SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestActivatedEvent);
			}
			if (QuestPair.Value.bWatchQuestStart)
			{
				SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestStartedEvent);
			}
			if (QuestPair.Value.bWatchQuestStepStart)
			{
				SignalSubsystem->SubscribeMessage<FQuestStepStartedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestStepStartedEvent);
			}
			if (QuestPair.Value.bWatchQuestStepEnd)
			{
				SignalSubsystem->SubscribeMessage<FQuestStepCompletedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestStepCompletedEvent);
			}
			if (QuestPair.Value.bWatchQuestEnd)
			{
				SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestWatcherComponent::WatchedQuestCompletedEvent);
			}
		}
	}
	else
	{
		if (GetOwner())
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : WatchedQuests is empty, registration failed. Actor: %s"), *GetOwner()->GetActorNameOrLabel());
		}
	}
}

