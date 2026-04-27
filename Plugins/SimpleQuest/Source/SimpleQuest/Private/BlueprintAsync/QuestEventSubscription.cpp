// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "BlueprintAsync/QuestEventSubscription.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestDisabledEvent.h"
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestProgressEvent.h"
#include "Events/QuestStartedEvent.h"
#include "GameplayTagsManager.h"
#include "Signals/SignalSubsystem.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestStateSubsystem.h"
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

    if (ExposedEventsMask == 0)
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestEventSubscription: '%s' has no exposed events — subscription is a no-op. ")
            TEXT("Enable at least one event under Pins | <phase> in the BindToQuestEvent node's Details panel."),
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

    // Subscribe only to the channels the K2 node has exposed. Each guard saves both the SubscribeMessage cost and
    // a per-event broadcast call when the corresponding pin doesn't exist on the consumer side.

    // Offer phase
    if (IsExposed(EQuestEventTypes::Activated))
    {
        ActivatedHandle = Signals->SubscribeMessage<FQuestActivatedEvent>(QuestTag, this, &UQuestEventSubscription::HandleActivated);
    }
    if (IsExposed(EQuestEventTypes::Enabled))
    {
        EnabledHandle = Signals->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestEventSubscription::HandleEnabled);
    }
    if (IsExposed(EQuestEventTypes::Disabled))
    {
        DisabledHandle = Signals->SubscribeMessage<FQuestDisabledEvent>(QuestTag, this, &UQuestEventSubscription::HandleDisabled);
    }
    if (IsExposed(EQuestEventTypes::GiveBlocked))
    {
        GiveBlockedHandle = Signals->SubscribeMessage<FQuestGiveBlockedEvent>(QuestTag, this, &UQuestEventSubscription::HandleGiveBlocked);
    }

    // Run phase
    if (IsExposed(EQuestEventTypes::Started))
    {
        StartedHandle = Signals->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestEventSubscription::HandleStarted);
    }
    if (IsExposed(EQuestEventTypes::Progress))
    {
        ProgressHandle = Signals->SubscribeMessage<FQuestProgressEvent>(QuestTag, this, &UQuestEventSubscription::HandleProgress);
    }
    
    // End phase
    if (IsExposed(EQuestEventTypes::Completed))
    {
        EndedHandle = Signals->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestEventSubscription::HandleEnded);
    }
    if (IsExposed(EQuestEventTypes::Deactivated) || IsExposed(EQuestEventTypes::Blocked))
    {
        DeactivatedHandle = Signals->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestEventSubscription::HandleDeactivated);
    }
    
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

void UQuestEventSubscription::HandleActivated(FGameplayTag Channel, const FQuestActivatedEvent& Event)
{
    if (bCancelled) return;
    bSawLiveActivated = true;
    if (OnActivated.IsBound()) OnActivated.Broadcast(Event.GetQuestTag(), Event.Context, Event.PrereqStatus);
}

void UQuestEventSubscription::HandleEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
    if (bCancelled) return;
    bSawLiveEnabled = true;
    if (OnEnabled.IsBound()) OnEnabled.Broadcast(Event.GetQuestTag(), Event.Context);
}

void UQuestEventSubscription::HandleDisabled(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
    if (bCancelled) return;
    if (OnDisabled.IsBound()) OnDisabled.Broadcast(Event.GetQuestTag(), Event.Context);
}

void UQuestEventSubscription::HandleGiveBlocked(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
    if (bCancelled) return;
    if (OnGiveBlocked.IsBound())
    {
        OnGiveBlocked.Broadcast(Event.GetQuestTag(), Event.Blockers, Event.GiverActor.Get());
    }
}

void UQuestEventSubscription::HandleStarted(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
    if (bCancelled) return;
    bSawLiveStarted = true;
    if (OnStarted.IsBound())
    {
        OnStarted.Broadcast(Event.GetQuestTag(), Event.Context, Event.GiverActor.Get());
    }
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

    // Blocked detection: SetQuestDeactivated writes both Deactivated AND Blocked facts on a SetBlocked-driven path,
    // and only Deactivated for an abandon/interrupt. Inspect the Blocked fact on the event's quest tag to decide
    // whether to broadcast OnBlocked alongside OnDeactivated. Avoids an FQuestDeactivatedEvent payload change.
    if (IsExposed(EQuestEventTypes::Blocked))
    {
        if (UWorldStateSubsystem* WorldState = ResolveWorldStateSubsystem())
        {
            const FGameplayTag BlockedFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(Event.GetQuestTag(), FQuestStateTagUtils::Leaf_Blocked), false);
            if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
            {
                bSawLiveBlocked = true;
                if (OnBlocked.IsBound()) OnBlocked.Broadcast(Event.GetQuestTag(), Event.Context);
            }
        }
    }

    if (IsExposed(EQuestEventTypes::Deactivated) && OnDeactivated.IsBound())
        OnDeactivated.Broadcast(Event.GetQuestTag(), Event.Context);
    // Persistent — no finalize here. Same rationale as HandleEnded.
}

void UQuestEventSubscription::HandleProgress(FGameplayTag Channel, const FQuestProgressEvent& Event)
{
    if (bCancelled) return;
    if (OnProgress.IsBound()) OnProgress.Broadcast(Event.GetQuestTag(), Event.Context);
}

void UQuestEventSubscription::UnbindAll()
{
    USignalSubsystem* Signals = ResolveSignalSubsystem();
    if (!Signals) return;
    if (ActivatedHandle.IsValid())   Signals->UnsubscribeMessage(QuestTag, ActivatedHandle);
    if (EnabledHandle.IsValid())     Signals->UnsubscribeMessage(QuestTag, EnabledHandle);
    if (DisabledHandle.IsValid())    Signals->UnsubscribeMessage(QuestTag, DisabledHandle);
    if (GiveBlockedHandle.IsValid()) Signals->UnsubscribeMessage(QuestTag, GiveBlockedHandle);
    if (StartedHandle.IsValid())     Signals->UnsubscribeMessage(QuestTag, StartedHandle);
    if (ProgressHandle.IsValid())    Signals->UnsubscribeMessage(QuestTag, ProgressHandle);
    if (EndedHandle.IsValid())       Signals->UnsubscribeMessage(QuestTag, EndedHandle);
    if (DeactivatedHandle.IsValid()) Signals->UnsubscribeMessage(QuestTag, DeactivatedHandle);
    ActivatedHandle = EnabledHandle = DisabledHandle = GiveBlockedHandle = FDelegateHandle();
    StartedHandle = ProgressHandle = EndedHandle = DeactivatedHandle = FDelegateHandle();
}

void UQuestEventSubscription::RunCatchUp(USignalSubsystem* Signals, UWorldStateSubsystem* WorldState)
{
    // Catch-up runs once, deferred to the tick after Activate(). Fires each pin if a matching state fact is already
    // set for QuestTag — gated by both ExposedEventsMask AND the bSawLive<Phase> flags so unexposed phases skip
    // entirely and exposed phases that already fired live during the deferral window don't double-broadcast.
    // Doesn't terminate the subscription — live events continue to flow through afterward, including for descendant
    // tags if QuestTag is a parent.
    FQuestEventContext SyntheticContext;
    SyntheticContext.NodeInfo.QuestTag = QuestTag;

    // PendingGiver fact gates both Activated and Enabled catch-up — quest is in giver state if-and-only-if
    // the fact is set. Activated fires regardless of prereq status; Enabled gates additionally on bSatisfied.
    const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(
        FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver), false);
    const bool bIsPendingGiver = PendingFact.IsValid() && WorldState->HasFact(PendingFact);

    FQuestPrereqStatus CachedPrereqStatus;
    if (bIsPendingGiver)
    {
        if (const UQuestStateSubsystem* StateSubsystem = ResolveQuestStateSubsystem())
        {
            CachedPrereqStatus = StateSubsystem->GetQuestPrereqStatus(QuestTag);
        }
    }

    if (IsExposed(EQuestEventTypes::Activated) && !bSawLiveActivated && bIsPendingGiver)
    {
        if (OnActivated.IsBound()) OnActivated.Broadcast(QuestTag, SyntheticContext, CachedPrereqStatus);
    }

    if (IsExposed(EQuestEventTypes::Enabled) && !bSawLiveEnabled && bIsPendingGiver && CachedPrereqStatus.bSatisfied)
    {
        if (OnEnabled.IsBound()) OnEnabled.Broadcast(QuestTag, SyntheticContext);
    }

    if (IsExposed(EQuestEventTypes::Started) && !bSawLiveStarted)
    {
        const FGameplayTag LiveFact = UGameplayTagsManager::Get().RequestGameplayTag(
            FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live), false);
        if (LiveFact.IsValid() && WorldState->HasFact(LiveFact))
        {
            // Catch-up GiverActor is null — the manager's RecentGiverActors entry was consumed at start time
            // and isn't recovered. Designers expecting historic giver attribution should subscribe live.
            if (OnStarted.IsBound()) OnStarted.Broadcast(QuestTag, SyntheticContext, nullptr);
        }
    }

    if (IsExposed(EQuestEventTypes::Completed) && !bSawLiveCompleted)
    {
        const FGameplayTag CompletedFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Completed), false);
        if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact))
        {
            FGameplayTag RecoveredOutcome = FGameplayTag::EmptyTag;
            if (const UQuestStateSubsystem* Resolution = ResolveQuestStateSubsystem())
            {
                if (const FQuestResolutionRecord* Record = Resolution->GetQuestResolution(QuestTag))
                {
                    RecoveredOutcome = Record->OutcomeTag;
                }
            }
            if (OnCompleted.IsBound()) OnCompleted.Broadcast(QuestTag, RecoveredOutcome, SyntheticContext);
        }
    }

    if (IsExposed(EQuestEventTypes::Deactivated) && !bSawLiveDeactivated)
    {
        const FGameplayTag DeactivatedFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Deactivated), false);
        if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
        {
            if (OnDeactivated.IsBound()) OnDeactivated.Broadcast(QuestTag, SyntheticContext);
        }
    }

    if (IsExposed(EQuestEventTypes::Blocked) && !bSawLiveBlocked)
    {
        const FGameplayTag BlockedFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Blocked), false);
        if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
        {
            if (OnBlocked.IsBound()) OnBlocked.Broadcast(QuestTag, SyntheticContext);
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

UQuestStateSubsystem* UQuestEventSubscription::ResolveQuestStateSubsystem() const
{
    if (UObject* Context = WorldContextObjectWeak.Get())
    {
        if (UWorld* World = Context->GetWorld())
        {
            if (UGameInstance* GI = World->GetGameInstance())
            {
                return GI->GetSubsystem<UQuestStateSubsystem>();
            }
        }
    }
    return nullptr;
}

