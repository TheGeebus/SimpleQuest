// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestTargetComponent.h"

#include "SimpleQuestLog.h"
#include "Events/QuestObjectiveInteracted.h"
#include "Events/QuestObjectiveKilled.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestStepCompletedEvent.h"
#include "Events/QuestStepStartedEvent.h"
#include "Signals/SignalSubsystem.h"


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
		OnQuestTargetActivatedDelegateHandle = SignalSubsystem->SubscribeTyped<FQuestStepStartedEvent>(GetOwner(), this, &UQuestTargetComponent::OnTargetActivated);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestTargetComponent::BeginPlay : Quest target registered: %s"), *GetOwner()->GetActorNameOrLabel());
	}
}

void UQuestTargetComponent::OnTargetActivated(const FQuestStepStartedEvent& StepStartedEvent)
{	
	if (CheckQuestSignalSubsystem())
	{
		UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestTargetComponent::OnTargetActivated : Event tag: %s : Event type: %s : Owner: %s"), *StepStartedEvent.EventTags.ToStringSimple(), *StepStartedEvent.StaticStruct()->GetFName().ToString(), *GetOwner()->GetClass()->GetFName().ToString());

		OnQuestTargetDeactivatedDelegateHandle = SignalSubsystem->SubscribeTyped<FQuestStepCompletedEvent>(GetOwner(), this, &UQuestTargetComponent::OnTargetDeactivated);
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
		SignalSubsystem->PublishTyped(UQuestTargetInterface::StaticClass(), FQuestObjectiveTriggered(GetOwner()));
	}
}

void UQuestTargetComponent::GetKilled(AActor* KillerActor)
{
	if (CheckQuestSignalSubsystem())
	{
		SignalSubsystem->PublishTyped<FQuestObjectiveTriggered>(UQuestTargetInterface::StaticClass(), FQuestObjectiveKilled(GetOwner(), KillerActor));
	}
}

void UQuestTargetComponent::GetInteracted(AActor* InteractingActor)
{
	if (CheckQuestSignalSubsystem())
	{
		SignalSubsystem->PublishTyped<FQuestObjectiveTriggered>(UQuestTargetInterface::StaticClass(), FQuestObjectiveInteracted(GetOwner(), InteractingActor));
	}
}
