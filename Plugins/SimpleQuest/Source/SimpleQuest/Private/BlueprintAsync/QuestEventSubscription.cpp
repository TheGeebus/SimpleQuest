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
#include "Subsystems/QuestResolutionSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
#include "WorldState/WorldStateSubsystem.h"


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
		TEXT("UQuestEventSubscription: could not resolve SignalSubsystem or WorldStateSubsystem from world context — aborting. ")
		TEXT("Common causes: BindToQuestEvent fired before the world finished initializing, or the WorldContextObject pin is wired to an actor whose UWorld isn't valid."));
        SetReadyToDestroy();
        return;
    }

    EnabledHandle = Signals->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestEventSubscription::HandleEnabled);
    StartedHandle = Signals->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestEventSubscription::HandleStarted);
    EndedHandle = Signals->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestEventSubscription::HandleEnded);
    DeactivatedHandle = Signals->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestEventSubscription::HandleDeactivated);

    // Defer the catch-up broadcast to next tick. The K2 node's standard async expansion calls Activate() *before*
    // firing the user's Then exec output — so any designer who wires the AsyncTask pin into a "Set <var>" off the
    // primary Then chain hasn't cached it yet at the moment Activate runs. If catch-up fires a lifecycle delegate
    // synchronously inside Activate (e.g., a quest already resolved before this binding), the user's downstream
    // chain (Print → Cancel(<var>)) reads the still-null cache → Accessed-None. Deferring to next tick guarantees
    // the standard expansion completes (Activate returns → ThenOut fires → user's Set node runs) before any
    // catch-up delegate fires. Mirrors the deferral pattern in engine async tasks like UAsyncTaskDownloadImage.
    UWorld* World = WorldContextObjectWeak.IsValid() ? WorldContextObjectWeak->GetWorld() : nullptr;
    if (!World)
    {
        UE_LOG(LogSimpleQuest, Verbose,
            TEXT("UQuestEventSubscription: no world available for deferred catch-up — running inline (acceptable: no BP execution stack to race with)."));
        RunCatchUp(Signals, WorldState);
        return;
    }

    TWeakObjectPtr<UQuestEventSubscription> WeakThis(this);
    World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
    {
        UQuestEventSubscription* Self = WeakThis.Get();
        if (!Self || Self->bCancelled) return;
        USignalSubsystem* DeferredSignals = Self->ResolveSignalSubsystem();
        UWorldStateSubsystem* DeferredWorldState = Self->ResolveWorldStateSubsystem();
        if (!DeferredSignals || !DeferredWorldState) return;
        Self->RunCatchUp(DeferredSignals, DeferredWorldState);
    }));
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
    bSawLiveActivated = true;
    if (OnActivated.IsBound()) OnActivated.Broadcast(Event.GetQuestTag(), Event.Context);
}

void UQuestEventSubscription::HandleStarted(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
    if (bCancelled) return;
    bSawLiveStarted = true;
    if (OnStarted.IsBound()) OnStarted.Broadcast(Event.GetQuestTag(), Event.Context);
}

void UQuestEventSubscription::HandleEnded(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
    if (bCancelled) return;
    bSawLiveCompleted = true;
    if (OnCompleted.IsBound()) OnCompleted.Broadcast(Event.GetQuestTag(), Event.OutcomeTag, Event.Context);
    // Persistent — no finalize here. Parent-tag subscriptions need to stay alive across multiple child completions.
}

void UQuestEventSubscription::HandleDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    if (bCancelled) return;
    bSawLiveDeactivated = true;
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
    // Catch-up runs once, deferred to the tick after Activate(). Fires each pin if a matching state fact is already
    // set for QuestTag — UNLESS that phase already broadcast live during the deferral window (bSawLive<Phase>),
    // in which case the listener has already received that transition and a second broadcast would be a duplicate.
    // Doesn't terminate the subscription — live events continue to flow through to all four pins afterward,
    // including for descendant tags if QuestTag is a parent.
    FQuestEventContext SyntheticContext;
    SyntheticContext.NodeInfo.QuestTag = QuestTag;

    if (!bSawLiveActivated)
    {
        const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(
            FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver), false);
        if (PendingFact.IsValid() && WorldState->HasFact(PendingFact))
        {
            if (OnActivated.IsBound()) OnActivated.Broadcast(QuestTag, SyntheticContext);
        }
    }

    if (!bSawLiveStarted)
    {
        const FGameplayTag LiveFact = UGameplayTagsManager::Get().RequestGameplayTag(
            FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live), false);
        if (LiveFact.IsValid() && WorldState->HasFact(LiveFact))
        {
            if (OnStarted.IsBound()) OnStarted.Broadcast(QuestTag, SyntheticContext);
        }
    }

    if (!bSawLiveCompleted)
    {
        const FGameplayTag CompletedFact = UGameplayTagsManager::Get().RequestGameplayTag(
            FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Completed), false);
        if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact))
        {
            FGameplayTag RecoveredOutcome = FGameplayTag::EmptyTag;
            if (const UQuestResolutionSubsystem* Resolution = ResolveQuestResolutionSubsystem())
            {
                if (const FQuestResolutionRecord* Record = Resolution->GetQuestResolution(QuestTag))
                {
                    RecoveredOutcome = Record->OutcomeTag;
                }
            }
            if (OnCompleted.IsBound()) OnCompleted.Broadcast(QuestTag, RecoveredOutcome, SyntheticContext);
        }
    }

    if (!bSawLiveDeactivated)
    {
        const FGameplayTag DeactivatedFact = UGameplayTagsManager::Get().RequestGameplayTag(
            FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Deactivated), false);
        if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
        {
            if (OnDeactivated.IsBound()) OnDeactivated.Broadcast(QuestTag, SyntheticContext);
        }
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

UQuestResolutionSubsystem* UQuestEventSubscription::ResolveQuestResolutionSubsystem() const
{
    if (UObject* Context = WorldContextObjectWeak.Get())
    {
        if (UWorld* World = Context->GetWorld())
        {
            if (UGameInstance* GI = World->GetGameInstance())
            {
                return GI->GetSubsystem<UQuestResolutionSubsystem>();
            }
        }
    }
    return nullptr;
}
