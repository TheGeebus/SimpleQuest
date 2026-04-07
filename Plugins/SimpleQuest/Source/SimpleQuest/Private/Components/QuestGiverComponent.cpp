// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestGiverComponent.h"

#include "SimpleQuestLog.h"
#include "Events/QuestEnabledEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Subsystems/QuestManagerSubsystem.h"

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
	if (!QuestTagsToGive.IsEmpty())
	{
		for (const FGameplayTag& QuestTag : QuestTagsToGive)
		{
			if (!QuestTag.IsValid()) { continue; }
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::RegisterQuestGiver : Registered giver for tag: %s on actor: %s"),
				*QuestTag.ToString(), *GetOwner()->GetName());
			SignalSubsystem->SubscribeTypedByTag<FQuestEnabledEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestEnabledEventReceived);
		}
	}
	else
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestGiverComponent::RegisterQuestGiver : QuestTagsToGive is empty. Actor: %s"),
			*GetOwner()->GetActorNameOrLabel());
	}
}

void UQuestGiverComponent::OnQuestEnabledEventReceived(const FQuestEnabledEvent& QuestEnabledEvent)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestEnabledEventReceived : Channel Object ID: %s : Event type: %s : Owner: %s"), *QuestEnabledEvent.ChannelObjectID.ToString(), *QuestEnabledEvent.StaticStruct()->GetFName().ToString(), *GetOwner()->GetClass()->GetFName().ToString());

	SetQuestGiverActivated(QuestEnabledEvent.GetQuestTag(), QuestEnabledEvent.bIsActivated);
}

void UQuestGiverComponent::GiveQuestByTag(const FGameplayTag& QuestTag)
{
	if (QuestTag.IsValid())
	{
		if (UQuestManagerSubsystem* QuestManager = GetWorld()->GetGameInstance()->GetSubsystem<UQuestManagerSubsystem>())
		{
			QuestManager->GiveNodeQuest(QuestTag);
		}
	}
}

void UQuestGiverComponent::SetQuestGiverActivated(const FGameplayTag& QuestTag, bool bIsQuestActive)
{
	if (bIsQuestActive)
	{
		EnabledQuestTags.AddTag(QuestTag);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::SetQuestGiverActivated : Quest enabled: %s"), *QuestTag.ToString());
	}
	else
	{
		EnabledQuestTags.RemoveTag(QuestTag);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::SetQuestGiverActivated : Quest disabled: %s"), *QuestTag.ToString());
	}
	OnQuestGiverActivated.Broadcast(bIsQuestActive, QuestTag, CanGiveAnyQuests());
}

bool UQuestGiverComponent::CanGiveAnyQuests() const
{
	return !EnabledQuestTags.IsEmpty();
}

bool UQuestGiverComponent::IsQuestEnabled(FGameplayTag QuestTag)
{
	return EnabledQuestTags.HasTag(QuestTag);
}

