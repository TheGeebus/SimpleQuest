// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "BlueprintAsync/QuestEventSubscription.h"

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
#include "Signals/SignalSubsystem.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "WorldState/WorldStateSubsystem.h"


void UQuestEventSubscription::Activate()
{
    if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
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
    if (IsExposed(EQuestEventTypes::Deactivated))
    {
        DeactivatedHandle = Signals->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestEventSubscription::HandleDeactivated);
    }
    if (IsExposed(EQuestEventTypes::Blocked))
    {
        BlockedHandle = Signals->SubscribeMessage<FQuestBlockedEvent>(QuestTag, this, &UQuestEventSubscription::HandleBlocked);
    }
    if (IsExposed(EQuestEventTypes::Unblocked))
    {
        UnblockedHandle = Signals->SubscribeMessage<FQuestUnblockedEvent>(QuestTag, this, &UQuestEventSubscription::HandleUnblocked);
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
    TagsWithLiveActivatedSeen.Add(Event.GetQuestTag());
    if (OnActivated.IsBound()) OnActivated.Broadcast(Event.GetQuestTag(), Channel, Event.Context, Event.PrereqStatus);
}

void UQuestEventSubscription::HandleEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveEnabledSeen.Add(Event.GetQuestTag());
    if (OnEnabled.IsBound()) OnEnabled.Broadcast(Event.GetQuestTag(), Channel, Event.Context);
}

void UQuestEventSubscription::HandleDisabled(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
    if (bCancelled) return;
    if (OnDisabled.IsBound()) OnDisabled.Broadcast(Event.GetQuestTag(), Channel, Event.Context);
}

void UQuestEventSubscription::HandleGiveBlocked(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
    if (bCancelled) return;
    if (OnGiveBlocked.IsBound())
    {
        OnGiveBlocked.Broadcast(Event.GetQuestTag(), Channel, Event.Blockers, Event.GiverActor.Get());
    }
}

void UQuestEventSubscription::HandleStarted(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveStartedSeen.Add(Event.GetQuestTag());
    if (OnStarted.IsBound())
    {
        OnStarted.Broadcast(Event.GetQuestTag(), Channel, Event.Context, Event.GiverActor.Get());
    }
}

void UQuestEventSubscription::HandleEnded(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveCompletedSeen.Add(Event.GetQuestTag());
    if (OnCompleted.IsBound()) OnCompleted.Broadcast(Event.GetQuestTag(), Channel, Event.OutcomeTag, Event.Context);
    // Persistent — no finalize here. Parent-tag subscriptions need to stay alive across multiple child completions.
}

void UQuestEventSubscription::HandleDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveDeactivatedSeen.Add(Event.GetQuestTag());

    // OnBlocked fires from its own direct subscription on FQuestBlockedEvent (HandleBlocked) — no longer
    // piggybacks on FQuestDeactivatedEvent + Blocked-fact inspection.

    if (OnDeactivated.IsBound()) OnDeactivated.Broadcast(Event.GetQuestTag(), Channel, Event.Context);
    // Persistent — no finalize here. Same rationale as HandleEnded.
}

void UQuestEventSubscription::HandleBlocked(FGameplayTag Channel, const FQuestBlockedEvent& Event)
{
    if (bCancelled) return;
    TagsWithLiveBlockedSeen.Add(Event.GetQuestTag());
    if (OnBlocked.IsBound()) OnBlocked.Broadcast(Event.GetQuestTag(), Channel, Event.Context);
}

void UQuestEventSubscription::HandleUnblocked(FGameplayTag Channel, const FQuestUnblockedEvent& Event)
{
    if (bCancelled) return;
    if (OnUnblocked.IsBound()) OnUnblocked.Broadcast(Event.GetQuestTag(), Channel, Event.Context);
}


void UQuestEventSubscription::HandleProgress(FGameplayTag Channel, const FQuestProgressEvent& Event)
{
    if (bCancelled) return;
    if (OnProgress.IsBound()) OnProgress.Broadcast(Event.GetQuestTag(), Channel, Event.Context);
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
    if (BlockedHandle.IsValid())     Signals->UnsubscribeMessage(QuestTag, BlockedHandle);
    if (UnblockedHandle.IsValid())   Signals->UnsubscribeMessage(QuestTag, UnblockedHandle);
    ActivatedHandle = EnabledHandle = DisabledHandle = GiveBlockedHandle = FDelegateHandle();
    StartedHandle = ProgressHandle = EndedHandle = DeactivatedHandle = FDelegateHandle();
    BlockedHandle = UnblockedHandle = FDelegateHandle();
}

void UQuestEventSubscription::RunCatchUp(USignalSubsystem* Signals, UWorldStateSubsystem* WorldState)
{
    // Catch-up runs once, deferred to the tick after Activate(). For an exact-tag subscription this fires per-pin
    // if a matching state fact is set for QuestTag itself; for a parent-prefix subscription (subscribed tag is an
    // unknown namespace OR a known wrapper container) it fans out to every known descendant via FQuestCatchUpFanout
    // and probes each one in turn, mirroring the signal bus's hierarchical broadcast on the live side. Each pin
    // is gated by ExposedEventsMask AND the per-tag TagsWith*Seen sets so unexposed phases skip entirely and
    // exposed phases that already fired live for that specific tag during the deferral window don't double-broadcast.
    // Doesn't terminate the subscription — live events continue to flow through afterward.
    UQuestStateSubsystem* StateSubsystem = ResolveQuestStateSubsystem();
    const TArray<FGameplayTag> CatchUpTags = FQuestCatchUpFanout::EnumerateTagsForCatchUp(QuestTag, StateSubsystem);

    for (const FGameplayTag& EachTag : CatchUpTags)
    {
        // Catch-up synthesizes a delivery for the EachTag perspective. QuestTag and MatchedChannel both equal
        // EachTag here — the synthetic broadcast represents "the canonical of this perspective" (QuestTag) and
        // "the channel from the subscription's perspective" (MatchedChannel) as the same thing, since we're
        // catching up to a known historical state, not delivering a live multi-channel publish.
        FQuestEventContext SyntheticContext;
        SyntheticContext.NodeInfo.QuestTag = EachTag;

        // PendingGiver fact gates both Activated and Enabled catch-up — quest is in giver state if-and-only-if
        // the fact is set. Activated fires regardless of prereq status; Enabled gates additionally on bSatisfied.
        const FGameplayTag PendingFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::PendingGiver);
        const bool bIsPendingGiver = PendingFact.IsValid() && WorldState->HasFact(PendingFact);

        FQuestPrereqStatus CachedPrereqStatus;
        if (bIsPendingGiver && StateSubsystem)
        {
            CachedPrereqStatus = StateSubsystem->GetQuestPrereqStatus(EachTag);
        }

        if (IsExposed(EQuestEventTypes::Activated) && !TagsWithLiveActivatedSeen.Contains(EachTag) && bIsPendingGiver)
        {
            if (OnActivated.IsBound()) OnActivated.Broadcast(EachTag, EachTag, SyntheticContext, CachedPrereqStatus);
        }

        if (IsExposed(EQuestEventTypes::Enabled) && !TagsWithLiveEnabledSeen.Contains(EachTag) && bIsPendingGiver && CachedPrereqStatus.bSatisfied)
        {
            if (OnEnabled.IsBound()) OnEnabled.Broadcast(EachTag, EachTag, SyntheticContext);
        }

        if (IsExposed(EQuestEventTypes::Started) && !TagsWithLiveStartedSeen.Contains(EachTag))
        {
            const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Live);
            if (LiveFact.IsValid() && WorldState->HasFact(LiveFact))
            {
                // GiverActor recovered from the registry's per-tag historical context (FQuestEntryArrival's
                // ActivationParamsSnapshot.ActivationSource captured at start time and preserved past consumption).
                AActor* RecoveredGiver = StateSubsystem ? StateSubsystem->GetLastGiverActor(EachTag) : nullptr;
                if (OnStarted.IsBound()) OnStarted.Broadcast(EachTag, EachTag, SyntheticContext, RecoveredGiver);
            }
        }

        if (IsExposed(EQuestEventTypes::Completed) && !TagsWithLiveCompletedSeen.Contains(EachTag))
        {
            const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Completed);
            if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact))
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
                if (OnCompleted.IsBound()) OnCompleted.Broadcast(EachTag, EachTag, RecoveredOutcome, SyntheticContext);
            }
        }

        if (IsExposed(EQuestEventTypes::Deactivated) && !TagsWithLiveDeactivatedSeen.Contains(EachTag))
        {
            const FGameplayTag DeactivatedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Deactivated);
            if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
            {
                if (OnDeactivated.IsBound()) OnDeactivated.Broadcast(EachTag, EachTag, SyntheticContext);
            }
        }

        if (IsExposed(EQuestEventTypes::Blocked) && !TagsWithLiveBlockedSeen.Contains(EachTag))
        {
            const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Blocked);
            if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
            {
                if (OnBlocked.IsBound()) OnBlocked.Broadcast(EachTag, EachTag, SyntheticContext);
            }
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

