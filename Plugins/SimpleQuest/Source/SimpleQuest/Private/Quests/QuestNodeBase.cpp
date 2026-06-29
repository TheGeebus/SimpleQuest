// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestNodeBase.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Quests/Types/PrereqLeafSubscription.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Events/QuestResolutionRecordedEvent.h"
#include "Events/QuestEntryRecordedEvent.h"


UWorld* UQuestNodeBase::GetWorld() const
{
    return CachedGameInstance.IsValid() ? CachedGameInstance->GetWorld() : nullptr;
}

void UQuestNodeBase::Activate(FGameplayTag InContextualTag)
{
    // Stash the cascade's event ID so subclasses (UPrereqGateNode) can read it during ActivateInternal for
    // per-event-ID dedup. PendingActivationContext was populated by the manager before Activate runs.
    LastIncomingEventID = PendingActivationContext.IncomingContext.OriginatingEventID;
    
    // Prerequisite bypass — a caller asked to activate ignoring prereqs (a deliberate jump / unlock / replay press).
    // Consume the one-shot flag and, crucially, cancel any deferral this instance is still holding from an earlier
    // armed activation. Without the cancel, that lingering subscription would fire a SECOND ActivateInternal later
    // when the skipped prereq belatedly satisfies (player jumps ahead, then finishes the chapter they skipped).
    if (bBypassPrerequisitesOnce)
    {
        bBypassPrerequisitesOnce = false;
        if (DeferredContextualTag.IsValid())
        {
            USignalSubsystem* Signals = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<USignalSubsystem>() : nullptr;
            FPrereqLeafSubscription::UnsubscribeAll(Signals, PrereqSubscriptionHandles);
            DeferredContextualTag = FGameplayTag::EmptyTag;
        }
        ActivateInternal(InContextualTag);
        return;
    }

    UWorldStateSubsystem* WorldState = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<UWorldStateSubsystem>() : nullptr;
    UQuestStateSubsystem* StateSubsystem = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    if (PrerequisiteExpression.IsAlways() || PrerequisiteExpression.Evaluate(WorldState, StateSubsystem))
    {
        ActivateInternal(InContextualTag);
        return;
    }
    DeferActivation(InContextualTag);
}

void UQuestNodeBase::ActivateInternal(FGameplayTag InContextualTag)
{
    OnNodeStarted.ExecuteIfBound(this, InContextualTag);
}

void UQuestNodeBase::DeactivateInternal(FGameplayTag InContextualTag)
{
    // Cancel any deferred prereq subscriptions that are still live
    if (DeferredContextualTag.IsValid())
    {
        USignalSubsystem* Signals = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<USignalSubsystem>() : nullptr;
        FPrereqLeafSubscription::UnsubscribeAll(Signals, PrereqSubscriptionHandles);
        DeferredContextualTag = FGameplayTag::EmptyTag;
    }
}

void UQuestNodeBase::ForwardActivation()
{
    OnNodeForwardActivated.ExecuteIfBound(this);
}

void UQuestNodeBase::ResetTransientState()
{
    // Handles reference a SignalSubsystem from the previous PIE session — now dead. Clearing the map without
    // unsubscribing is safe: the owning subsystem is gone, there's nothing left to unsubscribe from.
    PrereqSubscriptionHandles.Reset();
    DeferredContextualTag = FGameplayTag::EmptyTag;
    bWasGiverGated = false;
    bBypassPrerequisitesOnce = false;
    PendingActivationContext = FQuestObjectiveRuntimeContext{};
    LastIncomingEventID = FOriginatingEventID{};
}

void UQuestNodeBase::DeferActivation(FGameplayTag InContextualTag)
{
    // Cancel any prior deferred subscriptions before re-subscribing. Re-defer means "the cascade is asking us to
    // wait on prereqs again — latest takes precedence." Without this, the subscribe helper's MergeSlot overwrites
    // tracked handles in PrereqSubscriptionHandles with the new ones, orphaning the originals (still firing in the
    // SignalSubsystem, no longer tracked here, can't be cleaned up via UnsubscribeAll). The orphan would call
    // OnPrereq*Handler on future fact events, hitting TryActivateDeferred with a cleared DeferredContextualTag and
    // producing a spurious ActivateInternal(EmptyTag) → spurious Forward. Mirrors DeactivateInternal's cleanup idiom.
    USignalSubsystem* Signals = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<USignalSubsystem>() : nullptr;
    FPrereqLeafSubscription::UnsubscribeAll(Signals, PrereqSubscriptionHandles);

    DeferredContextualTag = InContextualTag;
    if (UseSymmetricPrereqSubscription())
    {
        // Symmetric path: Fact leaves get the Removed handler too, so NOT(Fact) wakes when the fact is removed.
        // Path / Resolution / Entry / Outcome leaves are append-only by registry shape — no corresponding "removed"
        // channel exists for them, so the symmetric overload degenerates to monotonic for those leaf kinds.
        FPrereqLeafSubscription::SubscribeLeavesForReevaluation(
            PrerequisiteExpression,
            this,
            &UQuestNodeBase::OnPrereqFactAdded,
            &UQuestNodeBase::OnPrereqFactRemoved,
            &UQuestNodeBase::OnPrereqResolutionRecorded,
            &UQuestNodeBase::OnPrereqEntryRecorded,
            PrereqSubscriptionHandles);
    }
    else
    {
        FPrereqLeafSubscription::SubscribeLeavesForReevaluation(
            PrerequisiteExpression,
            this,
            &UQuestNodeBase::OnPrereqFactAdded,
            &UQuestNodeBase::OnPrereqResolutionRecorded,
            &UQuestNodeBase::OnPrereqEntryRecorded,
            PrereqSubscriptionHandles);
    }
}

void UQuestNodeBase::OnPrereqFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("OnPrereqFactAdded: subscriber='%s' wokeOnChannel='%s' eventFactTag='%s'"),
    DeferredContextualTag.IsValid() ? *DeferredContextualTag.ToString() : TEXT("(utility)"),
    *Channel.ToString(),
    *Event.StateTag.ToString());

    // A WorldState fact-added event carries no quest event identity, so recover the identity of the resolution that
    // wrote this prerequisite's mirror fact from the quest state registry. The resolution and entry wake-paths take
    // this identity directly from their events; the fact wake-path is the only one that must look it up. Without it,
    // a Prerequisite Gate woken by this fact fires under a stale identity and fails to recognize the same resolution
    // arriving through its direct Enter wire, resolving its wrapper twice for a single completion.
    if (CachedGameInstance.IsValid())
    {
        if (UQuestStateSubsystem* StateSubsystem = CachedGameInstance->GetSubsystem<UQuestStateSubsystem>())
        {
            const FOriginatingEventID FactEventID = StateSubsystem->GetPathFactWriteEventID(Event.StateTag);
            if (FactEventID.IsValid())
            {
                LastIncomingEventID = FactEventID;
            }
        }
    }

    TryActivateDeferred();
}

void UQuestNodeBase::OnPrereqFactRemoved(FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event)
{
    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("OnPrereqFactRemoved: subscriber='%s' wokeOnChannel='%s' eventFactTag='%s'"),
    DeferredContextualTag.IsValid() ? *DeferredContextualTag.ToString() : TEXT("(utility)"),
    *Channel.ToString(),
    *Event.StateTag.ToString());
    TryActivateDeferred();
}

void UQuestNodeBase::OnPrereqResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event)
{
    LastIncomingEventID = Event.OriginatingEventID;
    UE_LOG(LogSimpleQuestActivation, Verbose,
        TEXT("OnPrereqResolutionRecorded: subscriber='%s' wokeOnChannel='%s' eventQuestTag='%s' eventGuid=%s"),
        DeferredContextualTag.IsValid() ? *DeferredContextualTag.ToString() : TEXT("(utility)"),
        *Channel.ToString(),
        *Event.GetQuestTag().ToString(),
        Event.OriginatingEventID.IsValid() ? *Event.OriginatingEventID.AuthoredNodeGuid.ToString(EGuidFormats::Short) : TEXT("(invalid)"));
    TryActivateDeferred();
}

void UQuestNodeBase::OnPrereqEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event)
{
    LastIncomingEventID = Event.OriginatingEventID;
    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("OnPrereqEntryRecorded: subscriber='%s' wokeOnChannel='%s' eventQuestTag='%s'"),
        DeferredContextualTag.IsValid() ? *DeferredContextualTag.ToString() : TEXT("(utility)"),
        *Channel.ToString(),
        *Event.GetQuestTag().ToString());
    TryActivateDeferred();
}

void UQuestNodeBase::TryActivateDeferred()
{
    if (!CachedGameInstance.IsValid()) return;
    UWorldStateSubsystem* WorldState = CachedGameInstance->GetSubsystem<UWorldStateSubsystem>();
    UQuestStateSubsystem* StateSubsystem = CachedGameInstance->GetSubsystem<UQuestStateSubsystem>();
    if (!WorldState || !StateSubsystem) return;

    const bool bSatisfied = PrerequisiteExpression.Evaluate(WorldState, StateSubsystem);
    UE_LOG(LogSimpleQuestActivation, Verbose,
        TEXT("TryActivateDeferred: subscriber='%s' expression %s — handles=%d"),
        DeferredContextualTag.IsValid() ? *DeferredContextualTag.ToString() : TEXT("(utility)"),
        bSatisfied ? TEXT("SATISFIED, will activate") : TEXT("UNSATISFIED, staying deferred"),
        PrereqSubscriptionHandles.Num());

    if (!bSatisfied) return;

    USignalSubsystem* Signals = CachedGameInstance->GetSubsystem<USignalSubsystem>();
    FPrereqLeafSubscription::UnsubscribeAll(Signals, PrereqSubscriptionHandles);

    const FGameplayTag TagToActivate = DeferredContextualTag;
    DeferredContextualTag = FGameplayTag::EmptyTag;
    ActivateInternal(TagToActivate);
}

void UQuestNodeBase::ResolveContextualTag(FName TagName)
{
    ContextualTag = UGameplayTagsManager::Get().RequestGameplayTag(TagName, false);
    NodeInfo.QuestTag = ContextualTag;
    if (!ContextualTag.IsValid())
    {
        UE_LOG(LogSimpleQuestActivation, Warning,
            TEXT("ResolveContextualTag: '%s' is not registered in the runtime tag manager — stale compiled node, skipping. ")
            TEXT("Recompile the owning questline to refresh; if the problem persists, use Stale Quest Tags (Window → Developer Tools → Debug)."),
            *TagName.ToString());
        return;
    }
    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ResolveContextualTag: %s → DisplayName='%s'"), *ContextualTag.ToString(), *NodeInfo.DisplayName.ToString());
}

void UQuestNodeBase::ResolveAssetScopedAliasTags(const TArray<FName>& TagNames)
{
    AssetScopedAliasTags.Reset();
    AssetScopedAliasTags.Reserve(TagNames.Num());

    UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
    for (const FName& TagName : TagNames)
    {
        const FGameplayTag Resolved = Manager.RequestGameplayTag(TagName, false);
        if (!Resolved.IsValid())
        {
            UE_LOG(LogSimpleQuestActivation, Warning,
                TEXT("ResolveAssetScopedAliasTags: '%s' is not registered in the runtime tag manager — stale compiled alias, skipping. ")
                TEXT("Recompile the owning questline to refresh; if the problem persists, use Stale Quest Tags (Window → Developer Tools → Debug)."),
                *TagName.ToString());
            continue;
        }
        AssetScopedAliasTags.Add(Resolved);
    }

    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ResolveAssetScopedAliasTags: %d alias(es) resolved for '%s'"),
        AssetScopedAliasTags.Num(),
        *ContextualTag.ToString());
}

const TArray<FName>* UQuestNodeBase::GetNextNodesForPath(FName PathIdentity) const
{
    const FQuestPathNodeList* List = NextNodesByPath.Find(PathIdentity);
    return List ? &List->NodeTags : nullptr;
}
