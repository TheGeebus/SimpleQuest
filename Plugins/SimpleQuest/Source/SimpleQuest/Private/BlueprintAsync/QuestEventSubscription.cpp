// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "BlueprintAsync/QuestEventSubscription.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "GameplayTagsManager.h"
#include "Signals/SignalSubsystem.h"
#include "SimpleQuestLog.h"
#include "Utilities/QuestStateTagUtils.h"
#include "WorldState/WorldStateSubsystem.h"


UQuestEventSubscription* UQuestEventSubscription::BindToQuestEvent(UObject* WorldContextObject, FGameplayTag QuestTag)
{
    UQuestEventSubscription* Sub = NewObject<UQuestEventSubscription>();
    Sub->WorldContextObjectWeak = WorldContextObject;
    Sub->QuestTag = QuestTag;
    return Sub;
}

void UQuestEventSubscription::Activate()
{
    if (!FQuestStateTagUtils::IsTagRegisteredInRuntime(QuestTag))
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestEventSubscription: stale or invalid QuestTag '%s' — aborting subscription."),
            *QuestTag.ToString());
        SetReadyToDestroy();
        return;
    }

    USignalSubsystem* Signals = ResolveSignalSubsystem();
    UWorldStateSubsystem* WorldState = ResolveWorldStateSubsystem();
    if (!Signals || !WorldState)
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestEventSubscription: could not resolve SignalSubsystem or WorldStateSubsystem from world context — aborting."));
        SetReadyToDestroy();
        return;
    }

    EnabledHandle     = Signals->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestEventSubscription::HandleEnabled);
    StartedHandle     = Signals->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestEventSubscription::HandleStarted);
    EndedHandle       = Signals->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestEventSubscription::HandleEnded);
    DeactivatedHandle = Signals->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestEventSubscription::HandleDeactivated);

    RunCatchUp(Signals, WorldState);
}

void UQuestEventSubscription::Cancel()
{
    if (bCancelled) return;
    bCancelled = true;
    UnbindAll();
    SetReadyToDestroy();
}

void UQuestEventSubscription::HandleEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
    if (bCancelled) return;
    if (OnActivated.IsBound()) OnActivated.Broadcast(Event.GetQuestTag(), Event.Context);
}

void UQuestEventSubscription::HandleStarted(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
    if (bCancelled) return;
    if (OnStarted.IsBound()) OnStarted.Broadcast(Event.GetQuestTag(), Event.Context);
}

void UQuestEventSubscription::HandleEnded(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
    if (bCancelled) return;
    if (OnCompleted.IsBound()) OnCompleted.Broadcast(Event.GetQuestTag(), Event.OutcomeTag, Event.Context);
    // Persistent — no finalize here. Parent-tag subscriptions need to stay alive across multiple child completions.
}

void UQuestEventSubscription::HandleDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    if (bCancelled) return;
    if (OnDeactivated.IsBound()) OnDeactivated.Broadcast(Event.GetQuestTag(), Event.Context);
    // Persistent — no finalize here. Same rationale as HandleEnded.
}

void UQuestEventSubscription::UnbindAll()
{
    USignalSubsystem* Signals = ResolveSignalSubsystem();
    if (!Signals) return;
    if (EnabledHandle.IsValid())     Signals->UnsubscribeMessage(QuestTag, EnabledHandle);
    if (StartedHandle.IsValid())     Signals->UnsubscribeMessage(QuestTag, StartedHandle);
    if (EndedHandle.IsValid())       Signals->UnsubscribeMessage(QuestTag, EndedHandle);
    if (DeactivatedHandle.IsValid()) Signals->UnsubscribeMessage(QuestTag, DeactivatedHandle);
    EnabledHandle = StartedHandle = EndedHandle = DeactivatedHandle = FDelegateHandle();
}

void UQuestEventSubscription::RunCatchUp(USignalSubsystem* Signals, UWorldStateSubsystem* WorldState)
{
    // Catch-up runs once at Activate(). Fires each pin if a matching state fact is already set for QuestTag.
    // Doesn't terminate the subscription — live events continue to flow through to all four pins afterward,
    // including for descendant tags if QuestTag is a parent.
    FQuestEventContext SyntheticContext;
    SyntheticContext.NodeInfo.QuestTag = QuestTag;

    const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(
        FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver), false);
    if (PendingFact.IsValid() && WorldState->HasFact(PendingFact))
    {
        if (OnActivated.IsBound()) OnActivated.Broadcast(QuestTag, SyntheticContext);
    }

    const FGameplayTag ActiveFact = UGameplayTagsManager::Get().RequestGameplayTag(
        FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Active), false);
    if (ActiveFact.IsValid() && WorldState->HasFact(ActiveFact))
    {
        if (OnStarted.IsBound()) OnStarted.Broadcast(QuestTag, SyntheticContext);
    }

    const FGameplayTag CompletedFact = UGameplayTagsManager::Get().RequestGameplayTag(
        FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Completed), false);
    if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact))
    {
        if (OnCompleted.IsBound()) OnCompleted.Broadcast(QuestTag, FGameplayTag::EmptyTag, SyntheticContext);
    }

    const FGameplayTag DeactivatedFact = UGameplayTagsManager::Get().RequestGameplayTag(
        FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Deactivated), false);
    if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
    {
        if (OnDeactivated.IsBound()) OnDeactivated.Broadcast(QuestTag, SyntheticContext);
    }
}

USignalSubsystem* UQuestEventSubscription::ResolveSignalSubsystem() const
{
    if (UObject* Context = WorldContextObjectWeak.Get())
    {
        if (UWorld* World = Context->GetWorld())
        {
            if (UGameInstance* GI = World->GetGameInstance())
            {
                return GI->GetSubsystem<USignalSubsystem>();
            }
        }
    }
    return nullptr;
}

UWorldStateSubsystem* UQuestEventSubscription::ResolveWorldStateSubsystem() const
{
    if (UObject* Context = WorldContextObjectWeak.Get())
    {
        if (UWorld* World = Context->GetWorld())
        {
            if (UGameInstance* GI = World->GetGameInstance())
            {
                return GI->GetSubsystem<UWorldStateSubsystem>();
            }
        }
    }
    return nullptr;
}