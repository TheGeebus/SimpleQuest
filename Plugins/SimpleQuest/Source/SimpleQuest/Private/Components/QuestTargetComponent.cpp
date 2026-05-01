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
#include "Utilities/QuestTagComposer.h"

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
        if (!FQuestTagComposer::IsTagRegisteredInRuntime(StepTag))
        {
            UE_LOG(LogSimpleQuest, Warning,
                TEXT("UQuestTargetComponent::BeginPlay : '%s' holds stale step tag '%s' — skipping subscribe. ")
                TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
                *GetOwner()->GetActorNameOrLabel(), *StepTag.ToString());
            continue;
        }
        FDelegateHandle Handle = SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(StepTag, this, &UQuestTargetComponent::OnTargetActivated);
        StepStartedHandles.Add(StepTag, Handle);
        UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestTargetComponent::BeginPlay : Watching step tag: %s on actor: %s"), *StepTag.ToString(), *GetOwner()->GetActorNameOrLabel());
    }
}

void UQuestTargetComponent::OnTargetActivated(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
    if (!SignalSubsystem) return;
    
    // Guard: ensure the channel matches some entry in StepTagsToWatch, including ancestor-matching semantics.
    if (!StepTagsToWatch.HasTag(Channel))
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestTargetComponent::OnTargetActivated : '%s' on '%s' — channel tag does not match any watched tag (including hierarchical descendants). Check StepTagsToWatch configuration."),
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
    
    UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestTargetComponent::OnTargetDeactivated : Channel: %s : Owner: %s"), *Channel.ToString(), *GetOwner()->GetClass()->GetFName().ToString());

    // Only visually deactivate when no steps remain active
    if (ActiveStepEndHandles.IsEmpty())
    {
        Execute_SetActivated(this, false);
    }
}

int32 UQuestTargetComponent::ApplyTagRenames(const TMap<FName, FName>& Renames)
{
    int32 Count = 0;
    for (const auto& [OldName, NewName] : Renames)
    {
        FGameplayTag FoundOld;
        for (const FGameplayTag& Tag : StepTagsToWatch.GetGameplayTagArray())
        {
            if (Tag.GetTagName() == OldName)
            {
                FoundOld = Tag;
                break;
            }
        }
        if (FoundOld.IsValid())
        {
            StepTagsToWatch.RemoveTag(FoundOld);
            FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(NewName, false);
            if (NewTag.IsValid())
            {
                StepTagsToWatch.AddTag(NewTag);
            }
            Count++;
        }
    }
    return Count;
}

int32 UQuestTargetComponent::RemoveTags(const TArray<FGameplayTag>& TagsToRemove)
{
    int32 Count = 0;
    for (const FGameplayTag& Tag : TagsToRemove)
    {
        if (StepTagsToWatch.HasTagExact(Tag))
        {
            if (Count == 0) Modify();
            StepTagsToWatch.RemoveTag(Tag);
            ++Count;
        }
    }
    if (Count > 0 && GetOwner())
    {
        GetOwner()->MarkPackageDirty();
    }
    return Count;
}

void UQuestTargetComponent::SetActivated_Implementation(bool bIsActivated)
{
    IQuestTargetInterface::SetActivated_Implementation(bIsActivated);
    OnQuestTargetActivated.Broadcast(bIsActivated);
}

void UQuestTargetComponent::SendTriggeredEvent(const FInstancedStruct& CustomData)
{
    if (!SignalSubsystem) return;
    if (ActiveStepEndHandles.IsEmpty()) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestTargetComponent_SendTriggeredEvent);
    
    UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestTargetComponent::SendTriggeredEvent : '%s' fanning out to %d watched step(s); CustomData %s"),
        *GetOwner()->GetActorNameOrLabel(), ActiveStepEndHandles.Num(), CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));

    TMap<FGameplayTag, FDelegateHandle> HandlesCopy = ActiveStepEndHandles;
    for (const auto& Pair : HandlesCopy)
    {
        SignalSubsystem->PublishMessage(Pair.Key, FQuestObjectiveTriggered(GetOwner(), nullptr, CustomData));
    }
}

void UQuestTargetComponent::SendKilledEvent(AActor* KillerActor, const FInstancedStruct& CustomData)
{
    if (!SignalSubsystem) return;
    if (ActiveStepEndHandles.IsEmpty()) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestTargetComponent_SendKilledEvent);

    UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestTargetComponent::SendKilledEvent : '%s' killed by '%s', fanning out to %d watched step(s); CustomData %s"),
        *GetOwner()->GetActorNameOrLabel(), KillerActor ? *KillerActor->GetActorNameOrLabel() : TEXT("(none)"),
        ActiveStepEndHandles.Num(), CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));

    for (const auto& Pair : ActiveStepEndHandles)
    {
        SignalSubsystem->PublishMessage(Pair.Key, FQuestObjectiveKilled(GetOwner(), KillerActor, CustomData));
    }
}

void UQuestTargetComponent::SendInteractedEvent(AActor* InteractingActor, const FInstancedStruct& CustomData)
{
    if (!SignalSubsystem) return;
    if (ActiveStepEndHandles.IsEmpty()) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestTargetComponent_SendInteractedEvent);

    UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestTargetComponent::SendInteractedEvent : '%s' interacted by '%s', fanning out to %d watched step(s); CustomData %s"),
        *GetOwner()->GetActorNameOrLabel(), InteractingActor ? *InteractingActor->GetActorNameOrLabel() : TEXT("(none)"),
        ActiveStepEndHandles.Num(), CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));

    for (const auto& Pair : ActiveStepEndHandles)
    {
        SignalSubsystem->PublishMessage(Pair.Key, FQuestObjectiveInteracted(GetOwner(), InteractingActor, CustomData));
    }
}

FGameplayTagContainer UQuestTargetComponent::GetRegisteredStepTagsToWatch() const
{
    return FQuestTagComposer::FilterToRegisteredTags(
        StepTagsToWatch,
        FString::Printf(TEXT("UQuestTargetComponent::GetRegisteredStepTagsToWatch ('%s')"),
            GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

