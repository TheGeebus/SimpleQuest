// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Components/QuestTargetComponent.h"
#include "SimpleQuestLog.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestObjectiveInteracted.h"
#include "Events/QuestObjectiveKilled.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Signals/SignalSubsystem.h"

UQuestTargetComponent::UQuestTargetComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UQuestTargetComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!SignalSubsystem) return;

    for (const FGameplayTag& StepTag : StepTagsToWatch)
    {
        if (!StepTag.IsValid()) continue;
        FDelegateHandle Handle = SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(StepTag, this, &UQuestTargetComponent::OnTargetActivated);
        StepStartedHandles.Add(StepTag, Handle);
        UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestTargetComponent::BeginPlay : Watching step tag: %s on actor: %s"), *StepTag.ToString(), *GetOwner()->GetActorNameOrLabel());
    }
}

void UQuestTargetComponent::OnTargetActivated(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
    if (!SignalSubsystem) return;

    UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestTargetComponent::OnTargetActivated : Channel: %s : Owner: %s"), *Channel.ToString(), *GetOwner()->GetClass()->GetFName().ToString());
    ActiveStepTag = Channel;
    StepCompletedHandle = SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(Channel, this, &UQuestTargetComponent::OnTargetDeactivated);
    Execute_SetActivated(this, true);
}

void UQuestTargetComponent::OnTargetDeactivated(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
    if (SignalSubsystem && StepCompletedHandle.IsValid())
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
    if (!SignalSubsystem) return;
    if (!ActiveStepTag.IsValid()) return; // Not active for any step, ignore

    SignalSubsystem->PublishMessage(ActiveStepTag, FQuestObjectiveTriggered(GetOwner()));
}

void UQuestTargetComponent::GetKilled(AActor* KillerActor)
{
    if (!SignalSubsystem) return;
    if (!ActiveStepTag.IsValid()) return;

    SignalSubsystem->PublishMessage(ActiveStepTag, FQuestObjectiveKilled(GetOwner(), KillerActor));
}

void UQuestTargetComponent::GetInteracted(AActor* InteractingActor)
{
    if (!SignalSubsystem) return;
    if (!ActiveStepTag.IsValid()) return;

    SignalSubsystem->PublishMessage(ActiveStepTag, FQuestObjectiveInteracted(GetOwner(), InteractingActor));
}

