// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/QuestGiverComponent.h"
#include "Quests/Quest.h"
#include "Events/QuestRegistrationEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Subsystems/QuestSignalSubsystem.h"

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
				QuestSignalSubsystem->SubscribeTyped<FQuestEnabledEvent>(LoadedQuestClass, this, &UQuestGiverComponent::OnQuestEnabledEventReceived);
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
	const FInstancedStruct Event = FInstancedStruct::Make<FQuestRegistrationEvent>(LoadedQuestClass->GetFName(), LoadedQuestClass, GetOwner());
	QuestSignalSubsystem->PublishTyped<FQuestRegistrationEvent>(UQuestGiverInterface::StaticClass(), Event);
}

void UQuestGiverComponent::OnQuestEnabledEventReceived(const FQuestEnabledEvent& QuestEnabledEvent)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestEnabledEventReceived : Channel Object ID: %s : Event type: %s : Owner: %s"), *QuestEnabledEvent.ChannelObjectID.ToString(), *QuestEnabledEvent.StaticStruct()->GetFName().ToString(), *GetOwner()->GetClass()->GetFName().ToString());

	SetQuestGiverActivated(QuestEnabledEvent.QuestClass, QuestEnabledEvent.ChannelObjectID, QuestEnabledEvent.bIsActivated);
}

void UQuestGiverComponent::StartQuest(UQuest* QuestToStart)
{
	if (QuestToStart)
	{
		if (const auto QuestClass = QuestToStart->GetClass())
		{
			if (CheckQuestSignalSubsystem())
			{
				FInstancedStruct QuestStartedEvent = FInstancedStruct::Make<FQuestStartedEvent>(QuestClass->GetFName(), QuestClass);
				QuestSignalSubsystem->PublishTyped<FQuestStartedEvent>(QuestClass, QuestStartedEvent);
				UE_LOG(LogSimpleQuest, Log, TEXT("UQuestGiverComponent::StartQuest : successfully started quest: %s"), *QuestClass->GetName());
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

bool UQuestGiverComponent::IsQuestEnabled(const FString& QuestID)
{
	for (auto Quest : EnabledQuests)
	{
		if (Quest.Value)
		{
			if (Quest.Value->GetQuestID() == QuestID)
			{
				UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::IsQuestEnabled : Found QuestID: %s"), *QuestID);
				return true;
			}
		}
	}
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::IsQuestEnabled : Did not find QuestID: %s"), *QuestID);
	return false;
}


