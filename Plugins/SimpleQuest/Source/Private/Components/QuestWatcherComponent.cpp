// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/QuestWatcherComponent.h"

#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestStepCompletedEvent.h"
#include "Events/QuestStepStartedEvent.h"
#include "Quests/Quest.h"
#include "Subsystems/QuestSignalSubsystem.h"


UQuestWatcherComponent::UQuestWatcherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestWatcherComponent::BeginPlay()
{
	Super::BeginPlay();

	RegisterQuestWatcher();
}

void UQuestWatcherComponent::WatchedQuestActivatedEvent(const FQuestEnabledEvent& QuestEnabledEvent)
{
	if (!QuestEnabledEvent.bIsActivated) { return; }
	const FQuestActiveStepIDs ActiveSteps;
	ActivatedQuestsMap.Add(QuestEnabledEvent.QuestClass) = ActiveSteps;
	if (OnQuestActivated.IsBound())
	{
		OnQuestActivated.Broadcast(QuestEnabledEvent.QuestClass->GetFName());
	}
}

void UQuestWatcherComponent::WatchedQuestStartedEvent(const FQuestStartedEvent& QuestStartedEvent)
{
	if (OnQuestStarted.IsBound())
	{
		OnQuestStarted.Broadcast(QuestStartedEvent.QuestClass->GetFName());
	}
}

void UQuestWatcherComponent::WatchedQuestStepStartedEvent(const FQuestStepStartedEvent& QuestStepStartedEvent)
{
	if (ActivatedQuestsMap.Contains(QuestStepStartedEvent.QuestClass))
	{
		ActivatedQuestsMap[QuestStepStartedEvent.QuestClass].ActiveStepIDs.Add(QuestStepStartedEvent.StepID);
	}
	if (OnQuestStepStarted.IsBound())
	{
		OnQuestStepStarted.Broadcast(QuestStepStartedEvent.QuestClass->GetFName(), QuestStepStartedEvent.StepID);
	}
}

void UQuestWatcherComponent::WatchedQuestStepCompletedEvent(const FQuestStepCompletedEvent& QuestStepCompletedEvent)
{
	if (ActivatedQuestsMap.Contains(QuestStepCompletedEvent.QuestClass))
	{
		ActivatedQuestsMap[QuestStepCompletedEvent.QuestClass].ActiveStepIDs.Remove(QuestStepCompletedEvent.StepID);
	}
	if (OnQuestStepCompleted.IsBound())
	{
		OnQuestStepCompleted.Broadcast(QuestStepCompletedEvent.QuestClass->GetFName(), QuestStepCompletedEvent.StepID, QuestStepCompletedEvent.bDidSucceed, QuestStepCompletedEvent.bEndedQuest);
	}
}

void UQuestWatcherComponent::WatchedQuestCompletedEvent(const FQuestEndedEvent& QuestEndedEvent)
{
	if (ActivatedQuestsMap.Contains(QuestEndedEvent.QuestClass))
	{
		ActivatedQuestsMap.Remove(QuestEndedEvent.QuestClass);
	}
	CompletedQuestClasses.Add(QuestEndedEvent.QuestClass.Get());
	if (OnQuestCompleted.IsBound())
	{
		OnQuestCompleted.Broadcast(QuestEndedEvent.QuestClass->GetFName(), QuestEndedEvent.bDidSucceed);
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
		for (auto QuestPair : WatchedQuests)
		{			
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : Registered quest watcher: %s"), *GetOwner()->GetName());
			if (UClass* OutQuestClass = QuestPair.Key.LoadSynchronous())
			{
				if (CheckQuestSignalSubsystem())
				{
					if (QuestPair.Value.bDoWatchQuestEnabled)
					{
						QuestSignalSubsystem->SubscribeTyped(OutQuestClass, this, &UQuestWatcherComponent::WatchedQuestActivatedEvent);
					}
					if (QuestPair.Value.bDoWatchQuestStart)
					{
						QuestSignalSubsystem->SubscribeTyped(OutQuestClass, this, &UQuestWatcherComponent::WatchedQuestStartedEvent);
					}
					if (QuestPair.Value.bDoWatchQuestStepStart)
					{
						QuestSignalSubsystem->SubscribeTyped(OutQuestClass, this, &UQuestWatcherComponent::WatchedQuestStepStartedEvent);
					}
					if (QuestPair.Value.bDoWatchQuestStepEnd)
					{
						QuestSignalSubsystem->SubscribeTyped(OutQuestClass, this, &UQuestWatcherComponent::WatchedQuestStepCompletedEvent);
					}
					if (QuestPair.Value.bDoWatchQuestEnd)
					{
						QuestSignalSubsystem->SubscribeTyped(OutQuestClass, this, &UQuestWatcherComponent::WatchedQuestCompletedEvent);
					}
				}
			}
		}
	}
	else
	{
		if (GetOwner())
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestWatcherComponent::RegisterQuestWatcher : QuestClassesToWatch is empty, registration failed. Actor: %s"), *GetOwner()->GetActorNameOrLabel());
		}
	}
}

