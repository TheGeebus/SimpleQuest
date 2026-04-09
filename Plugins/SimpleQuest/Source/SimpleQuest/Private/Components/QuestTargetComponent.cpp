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
    if (!CheckQuestSignalSubsystem()) return;

    for (const FGameplayTag& StepTag : StepTagsToWatch)
    {
        if (!StepTag.IsValid()) continue;
        FDelegateHandle Handle = SignalSubsystem->SubscribeMessage<FQuestStepStartedEvent>(StepTag, this, &UQuestTargetComponent::OnTargetActivated);
        StepStartedHandles.Add(StepTag, Handle);
        UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestTargetComponent::BeginPlay : Watching step tag: %s on actor: %s"), *StepTag.ToString(), *GetOwner()->GetActorNameOrLabel());
    }
}

void UQuestTargetComponent::OnTargetActivated(FGameplayTag Channel, const FQuestStepStartedEvent& StepStartedEvent)
{
    if (!CheckQuestSignalSubsystem()) return;

    UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestTargetComponent::OnTargetActivated : Channel: %s : Owner: %s"), *Channel.ToString(), *GetOwner()->GetClass()->GetFName().ToString());
    ActiveStepTag = Channel;
    StepCompletedHandle = SignalSubsystem->SubscribeMessage<FQuestStepCompletedEvent>(Channel, this, &UQuestTargetComponent::OnTargetDeactivated);
    Execute_SetActivated(this, true);
}

void UQuestTargetComponent::OnTargetDeactivated(FGameplayTag Channel, const FQuestStepCompletedEvent& StepCompletedEvent)
{
    if (CheckQuestSignalSubsystem() && StepCompletedHandle.IsValid())
    {
        SignalSubsystem->UnsubscribeMessage(ActiveStepTag, StepCompletedHandle);
        StepCompletedHandle.Reset();
        ActiveStepTag = FGameplayTag::EmptyTag;
    }
    Execute_SetActivated(this, false);
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
        SignalSubsystem->PublishMessage(Tag_Channel_QuestTarget, FQuestObjectiveTriggered(GetOwner()));
    }
}

void UQuestTargetComponent::GetKilled(AActor* KillerActor)
{
    if (CheckQuestSignalSubsystem())
    {
        SignalSubsystem->PublishMessage(Tag_Channel_QuestTarget, FQuestObjectiveKilled(GetOwner(), KillerActor));
    }
}

void UQuestTargetComponent::GetInteracted(AActor* InteractingActor)
{
    if (CheckQuestSignalSubsystem())
    {
        SignalSubsystem->PublishMessage(Tag_Channel_QuestTarget, FQuestObjectiveInteracted(GetOwner(), InteractingActor));
    }
}
