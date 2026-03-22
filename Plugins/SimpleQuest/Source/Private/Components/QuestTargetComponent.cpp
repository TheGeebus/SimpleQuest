// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/QuestTargetComponent.h"

#include "SimpleQuestLog.h"
#include "Events/QuestObjectiveInteracted.h"
#include "Events/QuestObjectiveKilled.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestStepCompletedEvent.h"
#include "Events/QuestStepStartedEvent.h"
#include "Subsystems/QuestSignalSubsystem.h"


UQuestTargetComponent::UQuestTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestTargetComponent::PostInitProperties()
{
	Super::PostInitProperties();

}

void UQuestTargetComponent::BeginPlay()
{
	Super::BeginPlay();
	if (CheckQuestSignalSubsystem())
	{
		OnQuestTargetActivatedDelegateHandle = QuestSignalSubsystem->SubscribeTyped<FQuestStepStartedEvent>(GetOwner(), this, &UQuestTargetComponent::OnTargetActivated);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestTargetComponent::BeginPlay : Quest target registered: %s"), *GetOwner()->GetActorNameOrLabel());
	}
}

void UQuestTargetComponent::OnTargetActivated(const FQuestStepStartedEvent& StepStartedEvent)
{	
	if (CheckQuestSignalSubsystem())
	{
		UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestTargetComponent::OnTargetActivated : Channel Object ID: %s : Event type: %s : Owner: %s"), *StepStartedEvent.ChannelObjectID.ToString(), *StepStartedEvent.StaticStruct()->GetFName().ToString(), *GetOwner()->GetClass()->GetFName().ToString());

		OnQuestTargetDeactivatedDelegateHandle = QuestSignalSubsystem->SubscribeTyped<FQuestStepCompletedEvent>(GetOwner(), this, &UQuestTargetComponent::OnTargetDeactivated);
		Execute_SetActivated(this, true);
	}
}

void UQuestTargetComponent::OnTargetDeactivated(const FQuestStepCompletedEvent& StepCompletedEvent)
{
	if (CheckQuestSignalSubsystem())
	{
		Execute_SetActivated(this, false);
	}
}

void UQuestTargetComponent::SetActivated_Implementation(bool bIsActivated)
{
	IQuestTargetInterface::SetActivated_Implementation(bIsActivated);

	OnQuestTargetActivated.Broadcast(bIsActivated);
}

void UQuestTargetComponent::GetTriggered()
{
	if (CheckQuestSignalSubsystem())
	{
		QuestSignalSubsystem->PublishTyped(UQuestTargetInterface::StaticClass(), FQuestObjectiveTriggered(FName(), nullptr, GetOwner()));
	}
}

void UQuestTargetComponent::GetKilled(AActor* KillerActor)
{
	if (CheckQuestSignalSubsystem())
	{
		QuestSignalSubsystem->PublishTyped<FQuestObjectiveTriggered>(UQuestTargetInterface::StaticClass(), FQuestObjectiveKilled(FName(), nullptr, GetOwner(), KillerActor));
	}
}

void UQuestTargetComponent::GetInteracted(AActor* InteractingActor)
{
	if (CheckQuestSignalSubsystem())
	{
		QuestSignalSubsystem->PublishTyped<FQuestObjectiveTriggered>(UQuestTargetInterface::StaticClass(), FQuestObjectiveInteracted(FName(), nullptr, GetOwner(), InteractingActor));
	}
}
