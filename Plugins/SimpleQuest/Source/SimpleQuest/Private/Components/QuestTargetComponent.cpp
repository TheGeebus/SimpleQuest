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
    
    // Guard: only respond to exact tag matches — parent tags in StepTagsToWatch would otherwise catch every descendant step's started event
    if (!StepTagsToWatch.HasTagExact(Channel))
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestTargetComponent::OnTargetActivated : '%s' on '%s' — channel tag is not an exact match for any watched tag. Check StepTagsToWatch configuration."),
            *Channel.ToString(), *GetOwner()->GetActorNameOrLabel());
        return;
    }
    
    // Guard against duplicate activation for the same step
    if (ActiveStepEndHandles.Contains(Channel)) return;

    FDelegateHandle Handle = SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(Channel, this, &UQuestTargetComponent::OnTargetDeactivated);
    ActiveStepEndHandles.Add(Channel, Handle);

    UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestTargetComponent::OnTargetActivated : Channel: %s : Owner: %s"), *Channel.ToString(), *GetOwner()->GetClass()->GetFName().ToString());

    Execute_SetActivated(this, true);
}

void UQuestTargetComponent::OnTargetDeactivated(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
    if (SignalSubsystem)
    {
        if (FDelegateHandle* Handle = ActiveStepEndHandles.Find(Channel))
        {
            SignalSubsystem->UnsubscribeMessage(Channel, *Handle);
        }
        ActiveStepEndHandles.Remove(Channel);
    }

    // Only visually deactivate when no steps remain active
    if (ActiveStepEndHandles.IsEmpty())
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
    if (!SignalSubsystem) return;
    if (ActiveStepEndHandles.IsEmpty()) return;

    TMap<FGameplayTag, FDelegateHandle> HandlesCopy = ActiveStepEndHandles;
    for (const auto& Pair : HandlesCopy)
    {
        SignalSubsystem->PublishMessage(Pair.Key, FQuestObjectiveTriggered(GetOwner()));
    }
}

void UQuestTargetComponent::GetKilled(AActor* KillerActor)
{
    if (!SignalSubsystem) return;
    if (ActiveStepEndHandles.IsEmpty()) return;

    for (const auto& Pair : ActiveStepEndHandles)
    {
        SignalSubsystem->PublishMessage(Pair.Key, FQuestObjectiveKilled(GetOwner(), KillerActor));
    }
}

void UQuestTargetComponent::GetInteracted(AActor* InteractingActor)
{
    if (!SignalSubsystem) return;
    if (ActiveStepEndHandles.IsEmpty()) return;

    for (const auto& Pair : ActiveStepEndHandles)
    {
        SignalSubsystem->PublishMessage(Pair.Key, FQuestObjectiveInteracted(GetOwner(), InteractingActor));
    }
}

