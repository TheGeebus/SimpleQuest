// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/QuestManagerSubsystem.h"

#include "SignalUtilities.h"
#include "Components/AudioComponent.h"
#include "Interfaces/QuestGiverInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Quests/Quest.h"
// #include "Engine/World.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestlineEndedEvent.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestRegistrationEvent.h"
#include "Events/QuestRewardEvent.h"
#include "Events/QuestStepCompletedEvent.h"
#include "Events/QuestStepStartedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestTryStartEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestStepPrereqCheckFailed.h"
#include "Interfaces/QuestTargetInterface.h"
#include "Interfaces/QuestWatcherInterface.h"
#include "Subsystems/QuestSignalSubsystem.h"


void UQuestManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		QuestSignalSubsystem = GameInstance->GetSubsystem<UQuestSignalSubsystem>();
		if (QuestSignalSubsystem)
		{
			QuestWatcherRegistrationDelegateHandle = QuestSignalSubsystem->SubscribeTyped<FQuestRegistrationEvent>(UQuestWatcherInterface::StaticClass(), this, &UQuestManagerSubsystem::RegisterQuestWatcher);
			QuestGiverRegistrationDelegateHandle = QuestSignalSubsystem->SubscribeTyped<FQuestRegistrationEvent>(UQuestGiverInterface::StaticClass(), this, &UQuestManagerSubsystem::RegisterQuestGiver);
			ObjectiveTriggeredDelegateHandle = QuestSignalSubsystem->SubscribeTyped<FQuestObjectiveTriggered>(UQuestTargetInterface::StaticClass(), this, &UQuestManagerSubsystem::CheckQuestObjectives);
		}
	}
	UE_LOG(LogSimpleQuest, Log, TEXT("UQuestManagerSubsystem::Initialize : Initializing: %s"), *GetFullName());
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UQuestManagerSubsystem::StartInitialQuests);
	}
}

void UQuestManagerSubsystem::Deinitialize()
{
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->UnsubscribeTyped<FQuestRegistrationEvent>(UQuestGiverInterface::StaticClass(), QuestGiverRegistrationDelegateHandle);
		QuestSignalSubsystem->UnsubscribeTyped<FQuestObjectiveTriggered>(UQuestTargetInterface::StaticClass(), ObjectiveTriggeredDelegateHandle);
	}
	Super::Deinitialize();
}

void UQuestManagerSubsystem::StartInitialQuests_Implementation()
{
	if (!InitialQuests.IsEmpty())
	{
		for (const TSoftClassPtr<UQuest>& QuestSoftClass : InitialQuests)
		{
			if (const UClass* QuestClass = QuestSoftClass.LoadSynchronous())
			{
				UE_LOG(LogSimpleQuest, Log, TEXT("UQuestManagerSubsystem::HandleWorldBeginPlay : Quest loading: %s"), *QuestClass->GetFullName());
				ActivateQuestClass(QuestClass);
			}
			else
			{
				UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::Initialize : soft reference to class was invalid, loading failed"));
			}
		}
	}
}

void UQuestManagerSubsystem::RegisterQuestGiver(const FQuestRegistrationEvent& Event)
{
	if (!Event.QuestClass)
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuestManagerSubsystem::RegisterQuestGiver : aborted, Quest was null."))
		return;
	}
	UClass* QuestClass = Event.QuestClass;
	UObject* QuestGiverObject = Event.OwningActor;
	
	if (LoadedQuestClasses.Contains(QuestClass))
	{
		UE_LOG(LogSimpleQuest, Log, TEXT("UQuestManagerSubsystem::RegisterQuestGiver : enabling quest: %s"), *QuestClass->GetFName().ToString());
		SetQuestEnabled(Event.ChannelObjectID, QuestClass, true);
	}
	if (!QuestGiverMap.Contains(QuestClass))
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::RegisterQuestGiver : Adding to QuestGiverMap, added quest giver: %s"), *Event.OwningActor->GetActorNameOrLabel());
		FQuestGivers QuestGiverStruct;
		QuestGiverStruct.QuestGiverSet.Add(QuestGiverObject);
		QuestGiverMap.Add(QuestClass, QuestGiverStruct);
	}
	else
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::RegisterQuestGiver : Adding to QuestGiverMap, quest already exists, added quest giver: %s"), *Event.OwningActor->GetActorNameOrLabel());
		QuestGiverMap.Find(QuestClass)->QuestGiverSet.Add(QuestGiverObject);
	}
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestManagerSubsystem::RegisterQuestGiver : Channel Object ID: %s : Quest giver: %s : Owner: %s"), *Event.ChannelObjectID.ToString(), *Event.OwningActor->GetFullName(), *Event.OwningActor->GetActorNameOrLabel());

}

void UQuestManagerSubsystem::RegisterQuestWatcher(const FQuestRegistrationEvent& QuestWatcherRegistrationEvent)
{
	if (!QuestSignalSubsystem) { return; }
	if (UClass* QuestClass = QuestWatcherRegistrationEvent.QuestClass)
	{
		if (CompletedQuestClasses.Contains(QuestClass))
		{
			QuestSignalSubsystem->PublishTyped(QuestWatcherRegistrationEvent.OwningActor, FQuestEndedEvent(QuestClass->GetFName(), QuestClass, CompletedQuestClasses[QuestClass]));
		}
		if (LoadedQuestClasses.Contains(QuestClass))
		{
			QuestSignalSubsystem->PublishTyped(QuestWatcherRegistrationEvent.OwningActor, FQuestStartedEvent(QuestClass->GetFName(), QuestClass));
			UQuest* Quest = FindLoadedQuestByClass(QuestClass);
			if (!Quest->GetCurrentActiveSteps().IsEmpty())
			{
				for (const auto QuestStepID : Quest->GetCurrentActiveSteps())
				{
					QuestSignalSubsystem->PublishTyped(QuestClass, FQuestStepStartedEvent(QuestClass->GetFName(), QuestClass, QuestStepID));
				}
			}
		}
	}
}

void UQuestManagerSubsystem::OnTryQuestStartEvent(const FTryQuestStartEvent& Event)
{
	StartQuest(Event.QuestClass.Get());
}

UQuest* UQuestManagerSubsystem::LoadQuest(const TSoftClassPtr<UQuest>& QuestClass) 
{
	UClass* NewClass = QuestClass.LoadSynchronous();
	if (!NewClass)
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuestManagerSubsystem::LoadQuest : QuestClass was not set, LoadQuest will return null"))
		return nullptr;
	}
	if (LoadedQuestClasses.Contains(NewClass))
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::LoadQuest : QuestClass is already loaded, finding: %s"), *NewClass->GetClass()->GetFName().ToString());
		for (auto Quest : LoadedQuests)
		{
			if (Quest.IsA(NewClass))
			{
				UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::LoadQuest : QuestClass found: %s"), *Quest->GetClass()->GetFName().ToString());
				return Quest;
			}
		}
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuestManagerSubsystem::LoadQuest : aborting, failed to find: %s"), *NewClass->GetClass()->GetFName().ToString());
		return nullptr;
	}
	
	UQuest* NewQuest = NewObject<UQuest>(this, NewClass);
	if (NewQuest != nullptr)
	{
		const FName NewID = NewQuest->GetQuestID();
		for (const UQuest* Existing : LoadedQuests)
		{
			if (Existing->GetQuestID() == NewID)
			{
				UE_LOG(LogSimpleQuest, Error,
					TEXT("QuestID '%s' collision: '%s' and '%s' share the same ID. Save data may be corrupted."),
					*NewID.ToString(),
					*Existing->GetClass()->GetPathName(),
					*NewClass->GetPathName());
			}
		}
		
		LoadedQuestClasses.Add(NewQuest->GetClass());
		LoadedQuests.Add(NewQuest);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::LoadQuest : Quest Loaded, class: %s"), *QuestClass->GetName());
		for (auto Pair : QuestGiverMap)
		{
			if (Pair.Key.IsValid())
			{
				UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestManagerSubsystem::LoadQuest : QuestGiverMap contains quest: %s"), *Pair.Key->GetName());
			}
		}

		NewQuest->OnUpdateQuestText.BindDynamic(this, &UQuestManagerSubsystem::UpdateQuestText);
		NewQuest->OnSetQuestTextVisibility.BindDynamic(this, &UQuestManagerSubsystem::UpdateQuestTextVisibility);
		NewQuest->OnQueueCommsEvent.BindDynamic(this, &UQuestManagerSubsystem::QueueCommsEvent);
		NewQuest->OnQuestStepStarted.BindDynamic(this, &UQuestManagerSubsystem::OnQuestStepStartedEvent);
		NewQuest->OnQuestStepPrereqsFail.BindDynamic(this, &UQuestManagerSubsystem::OnQuestStepPrereqCheckFail);
		NewQuest->OnQuestStepComplete.BindDynamic(this, &UQuestManagerSubsystem::OnQuestStepEndedEvent);
		NewQuest->OnQuestComplete.BindDynamic(this, &UQuestManagerSubsystem::CompleteQuest);
		NewQuest->OnObjectiveEnabled.BindUObject(this, &UQuestManagerSubsystem::OnQuestTargetEnabledEvent);
		if (NewQuest->DoesCompleteQuestline())
		{
			NewQuest->OnQuestlineComplete.BindDynamic(this, &UQuestManagerSubsystem::CompleteQuestline);
		}
	}
	else
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuestManagerSubsystem::LoadQuest : Quest not instantiated correctly, LoadQuest will return null"));
	}
	return NewQuest;
}

void UQuestManagerSubsystem::OnQuestStepStartedEvent(UQuest* ActiveQuest, int32 StartedQuestStepID)
{
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(ActiveQuest->GetClass(), FQuestStepStartedEvent(ActiveQuest->GetQuestID(), ActiveQuest->GetClass(), StartedQuestStepID));
	}
}

void UQuestManagerSubsystem::OnQuestStepPrereqCheckFail(UQuest* ActiveQuest, int32 StepWithPrereqsID)
{
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(ActiveQuest->GetClass(), FQuestStepPrereqCheckFailed(ActiveQuest->GetQuestID(), ActiveQuest->GetClass(), StepWithPrereqsID));
	}
}

void UQuestManagerSubsystem::OnQuestStepEndedEvent(UQuest* ActiveQuest, int32 CompletedQuestStep, bool bDidSucceed, bool bEndedQuest, UQuestReward* Reward)
{
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(ActiveQuest->GetClass(), FQuestStepCompletedEvent(ActiveQuest->GetQuestID(), ActiveQuest->GetClass(), CompletedQuestStep, bDidSucceed, bEndedQuest, Reward));
		QuestSignalSubsystem->PublishTyped(ActiveQuest->GetClass(), FQuestRewardEvent(ActiveQuest->GetQuestID(), ActiveQuest->GetClass(), Reward));
	}
}

void UQuestManagerSubsystem::PublishQuestEndEvent(const UQuest* EndedQuest, bool bDidSucceed) const
{
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(EndedQuest->GetClass(), FQuestEndedEvent(EndedQuest->GetQuestID(), EndedQuest->GetClass(), bDidSucceed));
	}
}

void UQuestManagerSubsystem::SetQuestEnabled(const FName LoadedQuestID, const TSubclassOf<UQuest>& LoadedQuestClass, const bool bIsEnabled)
{
	if (QuestSignalSubsystem)
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::SetQuestEnabled : publishing FQuestEnabledEvent for: %s"), *LoadedQuestClass->GetFName().ToString());

		QuestSignalSubsystem->PublishTyped(LoadedQuestClass, FQuestEnabledEvent(LoadedQuestID, LoadedQuestClass, bIsEnabled));
		QuestSignalSubsystem->SubscribeTyped<FTryQuestStartEvent>(LoadedQuestClass, this, &UQuestManagerSubsystem::OnTryQuestStartEvent);
	}
}

void UQuestManagerSubsystem::ActivateQuestClass(const TSoftClassPtr<UQuest>& InQuestClass)
{
	UQuest* LoadedQuest = LoadQuest(InQuestClass);
	if (LoadedQuest != nullptr && !StartedQuestClasses.Contains(LoadedQuest->GetClass()))
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::ActivateQuestClass : enabling quest: %s"), *LoadedQuest->GetQuestID().ToString());
		SetQuestEnabled(LoadedQuest->GetQuestID(), LoadedQuest->GetClass(), true);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::ActivateQuestClass : Quest loaded: %s"), *LoadedQuest->GetQuestID().ToString());
		if (!QuestGiverMap.Contains(LoadedQuest->GetClass()))
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("UQuestManagerSubsystem::ActivateQuestClass : no quest givers found, starting: %s"), *LoadedQuest->GetClass()->GetName());
			StartQuest(LoadedQuest->GetClass());
		}
		else
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("UQuestManagerSubsystem::ActivateQuestClass : quest givers found, activated but did not start: %s"), *LoadedQuest->GetClass()->GetName());
		}
	}
}

UQuest* UQuestManagerSubsystem::FindLoadedQuestByClass(const TSubclassOf<UQuest>& InQuest)
{
	for (auto Quest : LoadedQuests)
	{
		bool bMatched = false;
		if (Quest->IsA(InQuest))
		{
			bMatched = true;
		}
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::FindLoadedQuest : Checked %s against %s. Matched: %s"), *InQuest->GetName(), *Quest->GetClass()->GetName(), bMatched ? TEXT("true") : TEXT("false"));
		if (bMatched)
		{
			return Quest;
		}
	}
	return nullptr;
}

bool UQuestManagerSubsystem::StartQuest_Implementation(const TSoftClassPtr<UQuest>& InQuestClass) // TODO: Don't reenable quests that are already enabled
{
	if (StartedQuestClasses.Contains(InQuestClass.LoadSynchronous()))
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::StartQuest : Quest is already loaded, aborting..."));
		return false;
	}
	UQuest* StartedQuest = LoadQuest(InQuestClass);
	if (StartedQuest == nullptr)
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuestManagerSubsystem::StartQuest : LoadQuest returned nullptr in StartQuest, aborting..."));
		return false;
	}
	
	if (!ArePrerequisitesComplete(StartedQuest))
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::StartQuest : Prerequisites are not complete, aborting..."));
		if (QuestSignalSubsystem)
		{
			QuestSignalSubsystem->PublishTyped(InQuestClass.Get(), FQuestPrerequisiteCheckFailed(StartedQuest->GetQuestID(), InQuestClass.Get()));
		}
		return false;
	}

	UE_LOG(LogSimpleQuest, Log, TEXT("UQuestManagerSubsystem::StartQuest : Starting quest: %s"), *StartedQuest->GetName());
	StartedQuestClasses.Add(StartedQuest->GetClass());
	DeactivateQuestGivers(StartedQuest);
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(StartedQuest->GetClass(), FQuestStartedEvent(StartedQuest->GetQuestID(), StartedQuest->GetClass()));
	}
	StartedQuest->OnUpdateQuestText.BindDynamic(this, &UQuestManagerSubsystem::UpdateQuestText); // redundant?
	StartedQuest->StartQuestStep(0);
	return true;
}

// Remove the quest from the quest giver map and deactivate all mapped quest givers
void UQuestManagerSubsystem::DeactivateQuestGivers(const UQuest* DeactivatedQuest) const
{
	if (QuestGiverMap.Contains(DeactivatedQuest->GetClass()))
	{
		if (QuestSignalSubsystem != nullptr)
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::DeactivateQuestGivers : publishing FQuestEnabledEvent for: %s"), *DeactivatedQuest->GetQuestID().ToString());
			QuestSignalSubsystem->PublishTyped(DeactivatedQuest->GetClass(), FQuestEnabledEvent(DeactivatedQuest->GetQuestID(), DeactivatedQuest->GetClass(), false));
		}
	}
}

bool UQuestManagerSubsystem::ArePrerequisitesComplete(UQuest* QuestToCheck) const
{
	const bool bCheckForSuccess = QuestToCheck->ShouldQuestPrerequisitesSucceed();
	for (auto PrereqClass : QuestToCheck->GetPrerequisiteQuests())
	{
		if (PrereqClass.IsValid())
		{
			if (const bool* ValuePtr = CompletedQuestClasses.Find(PrereqClass.Get()))
			{
				if (bCheckForSuccess && !*ValuePtr)
				{
					return false; // if prereqs must succeed, check the value from the map and return early if it is false
				}
			}
			else
			{
				return false; // any uncompleted prerequisite will return early from this function
			}
		}
		else
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::StartQuest : Prerequisite class is not valid. Does it need to be loaded first?"));
			return false;
		}
	}
	return true;
}

void UQuestManagerSubsystem::CountQuestElement_Implementation(UObject* InQuestElement)
{
	for (auto Quest : LoadedQuests)
	{
		if (Quest)
		{
			Quest->CheckQuestTarget(InQuestElement);
		}
	}
}

void UQuestManagerSubsystem::CheckQuestObjectives(const FQuestObjectiveTriggered& QuestObjectiveEvent)
{
	TArray<UQuest*> LoadedQuestsCopy = LoadedQuests;
	for (const auto Quest : LoadedQuestsCopy)
	{
		if (Quest)
		{
			Quest->CheckQuestTarget(QuestObjectiveEvent.TriggeredActor);
		}
	}
}

void UQuestManagerSubsystem::QueueCommsEvent(const FCommsEvent& InCommsEvent)
{
	const FCommsEvent NewEvent = InCommsEvent;
	CommsEventQueue.Add(NewEvent);
	if (GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(CommsEventTimerHandle))
	{
		StartCommsEvent();
	}
}

void UQuestManagerSubsystem::StartCommsEvent()
{
	if (CommsEventQueue.Num() <= 0) { return; }
	const FCommsEvent& CommsEvent = CommsEventQueue[0];
	USoundBase* Sound = CommsEvent.Sound.LoadSynchronous();
	float Duration = CommsEvent.ViewDuration;
	if (Sound != nullptr)
	{
		AudioComponent = UGameplayStatics::CreateSound2D(this, Sound);
		if (AudioComponent != nullptr)
		{
			AudioComponent->Play();
			if (!CommsEvent.bOverrideDuration)
			{
				Duration = Sound->GetDuration() + CommsEventSubtitleDelay;
			}
		}
		else
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::StartCommsEvent : Quest manager could not create audio component"))
		}
	}
	else
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::StartCommsEvent : Quest manager cannot play sound as it is null"))
	}
		
	FTimerDelegate TimerDelegate = FTimerDelegate::CreateUObject(this, &UQuestManagerSubsystem::CommsEventTimerEnd);
	GetWorld()->GetTimerManager().SetTimer(CommsEventTimerHandle,
		TimerDelegate,
		Duration,
		false);
	if (OnCommsEventStart.IsBound())
	{
		OnCommsEventStart.Broadcast(CommsEventQueue[0]);
	}
}

void UQuestManagerSubsystem::CommsEventTimerEnd()
{
	if (OnCommsEventEnd.IsBound())
	{
		OnCommsEventEnd.Broadcast();
	}
	CommsEventQueue.RemoveAt(0);
	StartCommsEvent();
}

void UQuestManagerSubsystem::UpdateQuestText(const FQuestText& InQuestText)
{
	if (OnQuestTextUpdated.IsBound())
	{
		OnQuestTextUpdated.Broadcast(InQuestText);
	}
}

void UQuestManagerSubsystem::UpdateQuestTextVisibility(bool bIsVisible, bool bUseCounter)
{
	if (OnQuestTextVisibilityUpdated.IsBound())
	{
		OnQuestTextVisibilityUpdated.Broadcast(bIsVisible, bUseCounter);
	}
}

void UQuestManagerSubsystem::CompleteQuest(UQuest* CompletedQuest, bool bDidSucceed)
{
	CompletedQuestClasses.Add(CompletedQuest->GetClass());
	PublishQuestEndEvent(CompletedQuest, bDidSucceed);
	if (OnQuestEnd.IsBound())
	{
		OnQuestEnd.Broadcast();
	}
	StartedQuestClasses.Remove(CompletedQuest->GetClass());
	LoadedQuests.Remove(CompletedQuest);
	LoadedQuestClasses.Remove(CompletedQuest->GetClass());

	for (auto Quest : CompletedQuest->GetPrerequisitesToReset())
	{
		CompletedQuestClasses.Remove(Quest.LoadSynchronous());
	}
	
	for (auto NewQuest : CompletedQuest->GetNextQuests())
	{
		ActivateQuestClass(NewQuest);
	}
}

void UQuestManagerSubsystem::CompleteQuestline(UQuest* CompletedQuest, bool bDidSucceed)
{
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(CompletedQuest->GetClass(), FQuestlineEndedEvent(CompletedQuest->GetQuestID(), CompletedQuest->GetClass(), bDidSucceed));
	}
	if (OnQuestlineEnd.IsBound())
	{
		OnQuestlineEnd.Broadcast();
	}
}

void UQuestManagerSubsystem::OnQuestTargetEnabledEvent(UQuest* InQuest, UObject* TargetObject, int32 InStepID,
	bool bNewIsEnabled)
{
	if (QuestSignalSubsystem)
	{
		if (bNewIsEnabled)
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::OnQuestTargetEnabledEvent : publishing quest step started event: %s"), *InQuest->GetQuestID().ToString());
			QuestSignalSubsystem->PublishTyped(TargetObject, FQuestStepStartedEvent(InQuest->GetQuestID(), InQuest->GetClass(), InStepID));
		}
		else
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::OnQuestTargetEnabledEvent : publishing quest step ended event: %s"), *InQuest->GetQuestID().ToString());
			QuestSignalSubsystem->PublishTyped(TargetObject, FQuestStepCompletedEvent(InQuest->GetQuestID(), InQuest->GetClass(), InStepID, true, false, nullptr));
		}
	}
}
