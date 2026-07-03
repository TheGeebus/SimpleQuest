// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "BlueprintAsync/QuestLifecycleObserver.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestBlockedEvent.h"
#include "Events/QuestDisabledEvent.h"
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestProgressEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestUnblockedEvent.h"
#include "Utilities/QuestCatchUpFanout.h"
#include "GameplayTagsManager.h"
#include "Subsystems/SignalSubsystem.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "Utilities/SignalChannelUtils.h"
#include "Subsystems/WorldStateSubsystem.h"


void UQuestLifecycleObserver::Activate()
{
    if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
    {
        UE_LOG(LogSimpleQuestSubscription, Warning,
            TEXT("UQuestLifecycleObserver: stale or invalid QuestTag '%s' — aborting subscription."),
            *QuestTag.ToString());
        SetReadyToDestroy();
        return;
    }

    if (ExposedEventsMask == 0)
    {
        UE_LOG(LogSimpleQuestSubscription, Warning,
            TEXT("UQuestLifecycleObserver: '%s' has no exposed events — subscription is a no-op. ")
            TEXT("Enable at least one event under Pins | <phase> in the ObserveQuestLifecycle node's Details panel."),
            *QuestTag.ToString());
        SetReadyToDestroy();
        return;
    }

    USignalSubsystem* Signals = ResolveSignalSubsystem();
    UWorldStateSubsystem* WorldState = ResolveWorldStateSubsystem();
    if (!Signals || !WorldState)
    {
        UE_LOG(LogSimpleQuestSubscription, Warning,
            TEXT("UQuestLifecycleObserver: could not resolve SignalSubsystem or WorldStateSubsystem from world context — aborting. ")
            TEXT("Common causes: ObserveQuestLifecycle fired before the world finished initializing, or the WorldContextObject pin is wired to an actor whose UWorld isn't valid."));
        SetReadyToDestroy();
        return;
    }

    // Subscribe only to the channels the K2 node has exposed. Each guard saves both the SubscribeMessage cost and
    // a per-event broadcast call when the corresponding pin doesn't exist on the consumer side. Routing applies
    // uniformly to all ten subscriptions — designer chose at the ObserveQuestLifecycle call site whether they want
    // hierarchical (Descendants) or narrow (ExactMatch) delivery for this subscription instance.

    // Offer phase
    if (IsExposed(EQuestEventTypes::Activated))
    {
        ActivatedHandle = Signals->SubscribeMessage<FQuestActivatedEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleActivated, Routing);
    }
    if (IsExposed(EQuestEventTypes::Enabled))
    {
        EnabledHandle = Signals->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleEnabled, Routing);
    }
    if (IsExposed(EQuestEventTypes::Disabled))
    {
        DisabledHandle = Signals->SubscribeMessage<FQuestDisabledEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleDisabled, Routing);
    }
    if (IsExposed(EQuestEventTypes::GiveBlocked))
    {
        GiveBlockedHandle = Signals->SubscribeMessage<FQuestGiveBlockedEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleGiveBlocked, Routing);
    }

    // Run phase
    if (IsExposed(EQuestEventTypes::Started))
    {
        StartedHandle = Signals->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleStarted, Routing);
    }
    if (IsExposed(EQuestEventTypes::Progress))
    {
        ProgressHandle = Signals->SubscribeMessage<FQuestProgressEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleProgress, Routing);
    }

    // End phase
    if (IsExposed(EQuestEventTypes::Completed))
    {
        EndedHandle = Signals->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleEnded, Routing);
    }
    if (IsExposed(EQuestEventTypes::Deactivated))
    {
        DeactivatedHandle = Signals->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleDeactivated, Routing);
    }
    if (IsExposed(EQuestEventTypes::Blocked))
    {
        BlockedHandle = Signals->SubscribeMessage<FQuestBlockedEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleBlocked, Routing);
    }
    if (IsExposed(EQuestEventTypes::Unblocked))
    {
        UnblockedHandle = Signals->SubscribeMessage<FQuestUnblockedEvent>(QuestTag, this, &UQuestLifecycleObserver::HandleUnblocked, Routing);
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
        UE_LOG(LogSimpleQuestSubscription, Verbose,
            TEXT("UQuestLifecycleObserver: no world available for deferred catch-up — running inline (acceptable: no BP execution stack to race with)."));
        RunCatchUp(Signals, WorldState);
        return;
    }

    TWeakObjectPtr<UQuestLifecycleObserver> WeakThis(this);
    World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
    {
        UQuestLifecycleObserver* Self = WeakThis.Get();
        if (!Self || Self->bCancelled) return;
        USignalSubsystem* DeferredSignals = Self->ResolveSignalSubsystem();
        UWorldStateSubsystem* DeferredWorldState = Self->ResolveWorldStateSubsystem();
        if (!DeferredSignals || !DeferredWorldState) return;
        Self->RunCatchUp(DeferredSignals, DeferredWorldState);
    }));
}

void UQuestLifecycleObserver::Cancel()
{
    if (bCancelled) return;
    bCancelled = true;
    UnbindAll();
    SetReadyToDestroy();
}

void UQuestLifecycleObserver::HandleActivated(FGameplayTag Channel, const FQuestActivatedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveActivatedSeen.Add(Event.GetQuestTag());
    if (OnActivated.IsBound()) OnActivated.Broadcast(Event.GetQuestTag(), Channel, Event.Payload, Event.PrereqStatus);
}

void UQuestLifecycleObserver::HandleEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveEnabledSeen.Add(Event.GetQuestTag());
    if (OnEnabled.IsBound()) OnEnabled.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
}

void UQuestLifecycleObserver::HandleDisabled(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
    if (bCancelled) return;
    if (OnDisabled.IsBound()) OnDisabled.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
}

void UQuestLifecycleObserver::HandleGiveBlocked(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
    if (bCancelled) return;
    if (OnGiveBlocked.IsBound())
    {
        OnGiveBlocked.Broadcast(Event.GetQuestTag(), Channel, Event.Blockers, Event.GiverActor.Get());
    }
}

void UQuestLifecycleObserver::HandleStarted(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveStartedSeen.Add(Event.GetQuestTag());
    if (OnStarted.IsBound())
    {
        OnStarted.Broadcast(Event.GetQuestTag(), Channel, Event.Payload, Event.GiverActor.Get());
    }
}

void UQuestLifecycleObserver::HandleEnded(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveCompletedSeen.Add(Event.GetQuestTag());
    if (OnCompleted.IsBound()) OnCompleted.Broadcast(Event.GetQuestTag(), Channel, Event.OutcomeTag, Event.Payload);
    // Persistent — no finalize here. Parent-tag subscriptions need to stay alive across multiple child completions.
}

void UQuestLifecycleObserver::HandleDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveDeactivatedSeen.Add(Event.GetQuestTag());

    // OnBlocked fires from its own direct subscription on FQuestBlockedEvent (HandleBlocked) — no longer
    // piggybacks on FQuestDeactivatedEvent + Blocked-fact inspection.

    if (OnDeactivated.IsBound()) OnDeactivated.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
    // Persistent — no finalize here. Same rationale as HandleEnded.
}

void UQuestLifecycleObserver::HandleBlocked(FGameplayTag Channel, const FQuestBlockedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveBlockedSeen.Add(Event.GetQuestTag());
    if (OnBlocked.IsBound()) OnBlocked.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
}

void UQuestLifecycleObserver::HandleUnblocked(FGameplayTag Channel, const FQuestUnblockedEvent& Event)
{
    if (bCancelled) return;
    if (OnUnblocked.IsBound()) OnUnblocked.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
}


void UQuestLifecycleObserver::HandleProgress(FGameplayTag Channel, const FQuestProgressEvent& Event)
{
    if (bCancelled) return;
    if (OnProgress.IsBound()) OnProgress.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
}

void UQuestLifecycleObserver::UnbindAll()
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
    if (BlockedHandle.IsValid())     Signals->UnsubscribeMessage(QuestTag, BlockedHandle);
    if (UnblockedHandle.IsValid())   Signals->UnsubscribeMessage(QuestTag, UnblockedHandle);
    ActivatedHandle = EnabledHandle = DisabledHandle = GiveBlockedHandle = FDelegateHandle();
    StartedHandle = ProgressHandle = EndedHandle = DeactivatedHandle = FDelegateHandle();
    BlockedHandle = UnblockedHandle = FDelegateHandle();
}

void UQuestLifecycleObserver::RunCatchUp(USignalSubsystem* Signals, UWorldStateSubsystem* WorldState)
{
    // Catch-up runs once, deferred to the tick after Activate(). For an exact-tag subscription this fires per-pin
    // if a matching state fact is set for QuestTag itself; for a parent-prefix subscription (subscribed tag is an
    // unknown namespace OR a known wrapper container) it fans out to every known descendant via FQuestCatchUpFanout
    // and probes each one in turn, mirroring the signal bus's hierarchical broadcast on the live side. Each pin
    // is gated by ExposedEventsMask AND the per-tag TagsWith*Seen sets so unexposed phases skip entirely and
    // exposed phases that already fired live for that specific tag during the deferral window don't double-broadcast.
    // Doesn't terminate the subscription — live events continue to flow through afterward.
    UQuestStateSubsystem* StateSubsystem = ResolveQuestStateSubsystem();
    const TArray<FGameplayTag> CatchUpTags = FQuestCatchUpFanout::EnumerateTagsForCatchUp(QuestTag, StateSubsystem, Routing);

    for (const FGameplayTag& EachTag : CatchUpTags)
    {
        // EachTag is the canonical for this catch-up entry (post-GetQuestTagsUnderPrefix's alias-resolution).
        // Build the channel set [canonical, ...aliases] and pick the best match for this subscription's bound
        // tag — same selection the live bus dispatcher uses, so subscribers see consistent MatchedChannel
        // values across catch-up and live deliveries (no need to branch by delivery path).
        TArray<FGameplayTag> ChannelSet;
        ChannelSet.Add(EachTag);
        if (StateSubsystem)
        {
            for (const FGameplayTag& AliasTag : StateSubsystem->GetAssetScopedAliasTagsForCanonical(EachTag))
            {
                ChannelSet.Add(AliasTag);
            }
        }
        const FGameplayTag MatchedChannel = FSignalChannelUtils::PickBestMatchChannel(ChannelSet, QuestTag);

        FQuestEventPayload SyntheticPayload;
        SyntheticPayload.NodeInfo.QuestTag = EachTag;
        SyntheticPayload.Delivery = EQuestEventDelivery::CatchUp;   // reconstruction, not a live transition

        // Activated / Started reconstruct from any fact proving the quest ENTERED SCOPE — PendingGiver, Live, OR the
        // append-only Completed anchor (a finished quest necessarily activated and went live to reach a resolved
        // state; Live/PendingGiver are transient and already cleared). Without the Completed branch a finished quest
        // replays only its Completed event, so presentation keyed on Activated/Started never reconstructs for completed
        // content. Enabled stays PendingGiver-only. Mirrors UQuestObserverComponent's catch-up so the K2 node and the
        // component replay identically.
        const FGameplayTag PendingFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::PendingGiver);
        const bool bIsPendingGiver = PendingFact.IsValid() && WorldState->HasFact(PendingFact);

        const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Live);
        const bool bIsLive = LiveFact.IsValid() && WorldState->HasFact(LiveFact);

        const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Completed);
        const bool bIsCompleted = CompletedFact.IsValid() && WorldState->HasFact(CompletedFact);

        FQuestPrereqStatus CachedPrereqStatus;
        if (bIsPendingGiver && StateSubsystem)
        {
            CachedPrereqStatus = StateSubsystem->GetQuestPrereqStatus(EachTag);
        }

        if (IsExposed(EQuestEventTypes::Activated) && !TagsWithLiveActivatedSeen.Contains(EachTag) && (bIsPendingGiver || bIsLive || bIsCompleted))
        {
            if (OnActivated.IsBound()) OnActivated.Broadcast(EachTag, MatchedChannel, SyntheticPayload, CachedPrereqStatus);
        }

        if (IsExposed(EQuestEventTypes::Enabled) && !TagsWithLiveEnabledSeen.Contains(EachTag) && bIsPendingGiver && CachedPrereqStatus.bSatisfied)
        {
            if (OnEnabled.IsBound()) OnEnabled.Broadcast(EachTag, MatchedChannel, SyntheticPayload);
        }

        if (IsExposed(EQuestEventTypes::Started) && !TagsWithLiveStartedSeen.Contains(EachTag))
        {
            if (bIsLive || bIsCompleted)
            {
                AActor* RecoveredGiver = StateSubsystem ? StateSubsystem->GetLastGiverActor(EachTag) : nullptr;
                if (OnStarted.IsBound()) OnStarted.Broadcast(EachTag, MatchedChannel, SyntheticPayload, RecoveredGiver);
            }
        }

        if (IsExposed(EQuestEventTypes::Completed) && !TagsWithLiveCompletedSeen.Contains(EachTag) && bIsCompleted)
        {
            FGameplayTag RecoveredOutcome = FGameplayTag::EmptyTag;
            if (StateSubsystem)
            {
                if (const FQuestResolutionRecord* Record = StateSubsystem->GetQuestResolution(EachTag))
                {
                    if (const FQuestResolutionEntry* Latest = Record->GetLatest())
                    {
                        RecoveredOutcome = Latest->OutcomeTag;
                    }
                }
            }
            if (OnCompleted.IsBound()) OnCompleted.Broadcast(EachTag, MatchedChannel, RecoveredOutcome, SyntheticPayload);
        }
        
        if (IsExposed(EQuestEventTypes::Deactivated) && !TagsWithLiveDeactivatedSeen.Contains(EachTag))
        {
            const FGameplayTag DeactivatedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Deactivated);
            if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
            {
                if (OnDeactivated.IsBound()) OnDeactivated.Broadcast(EachTag, MatchedChannel, SyntheticPayload);
            }
        }

        if (IsExposed(EQuestEventTypes::Blocked) && !TagsWithLiveBlockedSeen.Contains(EachTag))
        {
            const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Blocked);
            if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
            {
                if (OnBlocked.IsBound()) OnBlocked.Broadcast(EachTag, MatchedChannel, SyntheticPayload);
            }
        }
    }
}

USignalSubsystem* UQuestLifecycleObserver::ResolveSignalSubsystem() const
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

UWorldStateSubsystem* UQuestLifecycleObserver::ResolveWorldStateSubsystem() const
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

UQuestStateSubsystem* UQuestLifecycleObserver::ResolveQuestStateSubsystem() const
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

