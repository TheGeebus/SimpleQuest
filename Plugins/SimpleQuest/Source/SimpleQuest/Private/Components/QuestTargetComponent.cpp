// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Components/QuestTargetComponent.h"
#include "SimpleQuestLog.h"
#include "Events/QuestDeactivatedEvent.h"
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

    // Track active subscription by the canonical ContextualTag from Event.Context — invariant across multi-publish
    // channels (Channel varies by which publish chain the bus dispatched through; Context.NodeInfo.QuestTag is the
    // Step's canonical identity, set by AssembleEventContext to ContextualTag). This serves two ends:
    //   (1) Dedup when StepTagsToWatch contains both ContextualTag and alias forms for the same logical Step —
    //       multi-publish would otherwise hit both subscriptions and double-activate.
    //   (2) Route trigger publishes (SendTriggeredEvent etc.) on ContextualTag, which is what the manager's per-
    //       step FQuestObjectiveTriggered subscription is bound to — closing the cross-asset trigger flow that
    //       otherwise stalls when a target is bound through an alias only.
    // Falls back to Channel if Context isn't populated (defensive — preserves behavior for any publish path that
    // hasn't been routed through FQuestPublish::OnAllNodeTags yet).
    const FGameplayTag CanonicalTag = Event.Context.NodeInfo.QuestTag.IsValid()
        ? Event.Context.NodeInfo.QuestTag
        : Channel;

    // Guard against duplicate activation for the same step
    if (ActiveStepEndHandles.Contains(CanonicalTag)) return;

    // Subscribe to BOTH end-event types — completion and deactivation are independent signal paths and either
    // should end the target's active state. ActiveStepEndHandles tracks the completion-side handle (one slot
    // per Channel); the deactivation-side handle is tracked separately so each can be unsubscribed cleanly
    // when its corresponding event fires.
    FDelegateHandle EndedHandle = SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(CanonicalTag, this, &UQuestTargetComponent::OnTargetStepCompleted);
    FDelegateHandle DeactivatedHandle = SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(CanonicalTag, this, &UQuestTargetComponent::OnTargetStepDeactivated);
    ActiveStepEndHandles.Add(CanonicalTag, EndedHandle);
    ActiveStepDeactivatedHandles.Add(CanonicalTag, DeactivatedHandle);

    UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestTargetComponent::OnTargetActivated : Channel: %s, Canonical: %s : Owner: %s"),
        *Channel.ToString(), *CanonicalTag.ToString(), *GetOwner()->GetClass()->GetFName().ToString());

    Execute_SetActivated(this, true);
}

void UQuestTargetComponent::OnTargetStepCompleted(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
    OnTargetStepEnded(Channel);
}

void UQuestTargetComponent::OnTargetStepDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    OnTargetStepEnded(Channel);
}

void UQuestTargetComponent::OnTargetStepEnded(FGameplayTag Channel)
{
    if (SignalSubsystem)
    {
        if (FDelegateHandle* Handle = ActiveStepEndHandles.Find(Channel))
        {
            SignalSubsystem->UnsubscribeMessage(Channel, *Handle);
        }
        if (FDelegateHandle* Handle = ActiveStepDeactivatedHandles.Find(Channel))
        {
            SignalSubsystem->UnsubscribeMessage(Channel, *Handle);
        }
        ActiveStepEndHandles.Remove(Channel);
        ActiveStepDeactivatedHandles.Remove(Channel);
    }

    UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestTargetComponent::OnTargetStepEnded : Channel: %s : Owner: %s"),
        *Channel.ToString(),
        *GetOwner()->GetClass()->GetFName().ToString());

    // Only visually deactivate when no other watched steps remain active. Both subscription maps are kept in
    // sync (each (Channel, end-event-type) pair adds and removes together), so either map's emptiness is a
    // sufficient check — but using ActiveStepEndHandles keeps a single canonical source of truth.
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

