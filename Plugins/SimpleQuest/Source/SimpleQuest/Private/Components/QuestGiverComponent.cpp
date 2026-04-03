// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestGiverComponent.h"
#include "Quests/Quest.h"
#include "Events/QuestRegistrationEvent.h"
#include "Events/QuestTryStartEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Signals/SignalSubsystem.h"

UQuestGiverComponent::UQuestGiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestGiverComponent::BeginPlay()
{
	Super::BeginPlay();

	RegisterQuestGiver();
}


void UQuestGiverComponent::RegisterQuestGiver()
{
	if (!CheckQuestSignalSubsystem())
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuestGiverComponent::RegisterQuestGiver : QuestSignalSubsystem is null, aborting."));
		return;
	}
	if (!QuestClassesToGive.IsEmpty())
	{
		for (auto QuestClass : QuestClassesToGive)
		{
			if (UClass* LoadedQuestClass = QuestClass.LoadSynchronous())
			{
				UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::RegisterQuestGiver : Registered quest giver: %s"), *GetOwner()->GetName());
				SignalSubsystem->SubscribeTyped<FQuestEnabledEvent>(LoadedQuestClass, this, &UQuestGiverComponent::OnQuestEnabledEventReceived);
				RegisterForQuestClass(LoadedQuestClass);
			}
		}
	}
	else
	{
		if (GetOwner())
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestGiverComponent::RegisterQuestGiver: QuestClassesToGive is empty, registration failed. Actor: %s"), *GetOwner()->GetActorNameOrLabel());
		}
	}
}

void UQuestGiverComponent::RegisterForQuestClass(UClass* LoadedQuestClass)
{
	SignalSubsystem->PublishTyped(UQuestGiverInterface::StaticClass(), FQuestRegistrationEvent(LoadedQuestClass->GetDefaultObject<UQuest>()->GetQuestTag(), LoadedQuestClass, GetOwner()));
}

void UQuestGiverComponent::OnQuestEnabledEventReceived(const FQuestEnabledEvent& QuestEnabledEvent)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestEnabledEventReceived : Channel Object ID: %s : Event type: %s : Owner: %s"), *QuestEnabledEvent.ChannelObjectID.ToString(), *QuestEnabledEvent.StaticStruct()->GetFName().ToString(), *GetOwner()->GetClass()->GetFName().ToString());

	SetQuestGiverActivated(QuestEnabledEvent.QuestClass, QuestEnabledEvent.ChannelObjectID, QuestEnabledEvent.bIsActivated);
}

void UQuestGiverComponent::GiveQuest(UQuest* QuestToStart)
{
	if (QuestToStart)
	{
		if (const auto QuestClass = QuestToStart->GetClass())
		{
			if (CheckQuestSignalSubsystem())
			{
				SignalSubsystem->PublishTyped(QuestClass, FTryQuestStartEvent(QuestToStart->GetQuestTag(), QuestClass));
				UE_LOG(LogSimpleQuest, Log, TEXT("UQuestGiverComponent::StartQuest : attempting to start quest: %s"), *QuestClass->GetName());
			}
			else
			{
				UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestGiverComponent::StartQuest : Quest was not started, signal subsystem null, quest still enabled: %s"), *QuestClass->GetName());
			}
		}
		else
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestGiverComponent::StartQuest : Quest not yet enabled"));
		}
	}
	else
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestGiverComponent::StartQuest : Failed because NewQuestClass was null."));
	}
}

void UQuestGiverComponent::SetQuestGiverActivated(const TSubclassOf<UQuest>& QuestClassToEnable, const FName& QuestID, bool bIsQuestActive)
{
	const auto Quest = QuestClassToEnable->GetDefaultObject<UQuest>();
	if (bIsQuestActive)
	{		
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::SetQuestGiverActivated : Quest enabled: %s"), *QuestClassToEnable->GetName());
		EnabledQuests.Add(QuestClassToEnable, Quest);
	}
	else
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::SetQuestGiverActivated : Quest disabled, attempting to remove: %s"), *QuestClassToEnable->GetName());
		if (EnabledQuests.Remove(QuestClassToEnable) > 0)
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::SetQuestGiverActivated : Quest found and removed from enabled quests: %s"), *QuestClassToEnable->GetName());
		}
	}
	OnQuestGiverActivated.Broadcast(bIsQuestActive, Quest, CanGiveAnyQuests());
}

bool UQuestGiverComponent::CanGiveAnyQuests() const
{
	return !EnabledQuests.IsEmpty();
}

bool UQuestGiverComponent::IsQuestEnabled(const FGuid& QuestGuid)
{
	for (auto Quest : EnabledQuests)
	{
		if (Quest.Value)
		{
			if (Quest.Value->GetQuestGuid() == QuestGuid)
			{
				UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::IsQuestEnabled : Found QuestID: %s"), *Quest.Value->GetName());
				return true;
			}
		}
	}
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::IsQuestEnabled : Did not find QuestID: %s"), *QuestGuid.ToString());
	return false;
}


