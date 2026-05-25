// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Components/QuestTriggerComponent.h"
#include "SimpleQuestLog.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestTriggerFiredEvent.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "Quests/Types/QuestObservedTagSpec.h"
#include "Subsystems/SignalSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"

UQuestTriggerComponent::UQuestTriggerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UQuestTriggerComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!SignalSubsystem) return;

    for (const FGameplayTag& StepTag : StepTagsToTrigger)
    {
        if (!FQuestTagComposer::IsTagRegisteredInRuntime(StepTag))
        {
            UE_LOG(LogSimpleQuestSubscription, Warning,
                TEXT("UQuestTriggerComponent::BeginPlay : '%s' holds stale step tag '%s' — skipping subscribe. ")
                TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
                *GetOwner()->GetActorNameOrLabel(), *StepTag.ToString());
            continue;
        }
        SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(StepTag, this, &UQuestTriggerComponent::OnTriggerActivated);

        // Trigger-side per-fire + per-lifecycle subscriptions. Lifetime-of-component (vs Live-window-only for the
        // FQuestEndedEvent / FQuestDeactivatedEvent subs the OnTriggerActivated handler installs) because Blocked can
        // fire BEFORE the step ever goes Live (PendingGiver-with-structural-blockers case) and Deactivated needs to
        // catch the end transition regardless of which fire (if any) drove it.
        SignalSubsystem->SubscribeMessage<FQuestTriggerResponseEvent>(StepTag, this, &UQuestTriggerComponent::HandleQuestTriggerResponse);
        SignalSubsystem->SubscribeMessage<FQuestTriggerBlockedEvent>(StepTag, this, &UQuestTriggerComponent::HandleQuestTriggerBlocked);
        SignalSubsystem->SubscribeMessage<FQuestTriggerDeactivatedEvent>(StepTag, this, &UQuestTriggerComponent::HandleQuestTriggerDeactivated);

        UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("UQuestTriggerComponent::BeginPlay : Watching step tag: %s on actor: %s"), *StepTag.ToString(), *GetOwner()->GetActorNameOrLabel());
    }
    
    // Register this component as a Trigger source for its StepTagsToTrigger set. Observer registration runs in Super::BeginPlay
    // (RegisterQuestObserver). EndPlay (handled in the Observer base) strips every role entry pointing at this component.
    if (const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
    {
        if (UQuestStateSubsystem* StateSubsystem = GI->GetSubsystem<UQuestStateSubsystem>())
        {
            StateSubsystem->RegisterTriggerSource(this, StepTagsToTrigger);
        }
    }
}

void UQuestTriggerComponent::OnTriggerActivated(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
    if (!SignalSubsystem) return;
    
    // Guard: ensure the channel matches some entry in StepTagsToTrigger, including ancestor-matching semantics.
    if (!StepTagsToTrigger.HasTag(Channel))
    {
        UE_LOG(LogSimpleQuestSubscription, Warning,
            TEXT("UQuestTriggerComponent::OnTriggerActivated : '%s' on '%s' — channel tag does not match any watched tag (including hierarchical descendants). Check StepTagsToTrigger configuration."),
            *Channel.ToString(), *GetOwner()->GetActorNameOrLabel());
        return;
    }

    // Track active subscription by the canonical ContextualTag from Event.Context — invariant across multi-publish
    // channels (Channel varies by which publish chain the bus dispatched through; Context.NodeInfo.QuestTag is the
    // Step's canonical identity, set by AssembleEventContext to ContextualTag). This serves two ends:
    //   (1) Dedup when StepTagsToTrigger contains both ContextualTag and alias forms for the same logical Step —
    //       multi-publish would otherwise hit both subscriptions and double-activate.
    //   (2) Route trigger publishes (SendTriggerEvent) on ContextualTag, which is what the manager's per-step
    //       FQuestTriggerFiredEvent subscription is bound to — closing the cross-asset trigger flow that
    //       otherwise stalls when a target is bound through an alias only.
    // Falls back to Channel if Context isn't populated (defensive — preserves behavior for any publish path that
    // hasn't been routed through FQuestPublish::OnAllNodeTags yet).
    const FGameplayTag CanonicalTag = Event.Payload.NodeInfo.QuestTag.IsValid()
        ? Event.Payload.NodeInfo.QuestTag
        : Channel;

    // Guard against duplicate activation for the same step
    if (ActiveStepEndHandles.Contains(CanonicalTag)) return;

    // Subscribe to BOTH end-event types — completion and deactivation are independent signal paths and either
    // should end the target's active state. ActiveStepEndHandles tracks the completion-side handle (one slot
    // per Channel); the deactivation-side handle is tracked separately so each can be unsubscribed cleanly
    // when its corresponding event fires.
    FDelegateHandle EndedHandle = SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(CanonicalTag, this, &UQuestTriggerComponent::OnTriggerStepCompleted);
    FDelegateHandle DeactivatedHandle = SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(CanonicalTag, this, &UQuestTriggerComponent::OnTriggerStepDeactivated);
    ActiveStepEndHandles.Add(CanonicalTag, EndedHandle);
    ActiveStepDeactivatedHandles.Add(CanonicalTag, DeactivatedHandle);

    UE_LOG(LogSimpleQuestSubscription, VeryVerbose, TEXT("UQuestTriggerComponent::OnTriggerActivated : Channel: %s, Canonical: %s : Owner: %s"),
        *Channel.ToString(), *CanonicalTag.ToString(), *GetOwner()->GetClass()->GetFName().ToString());

    SetActivated(true);
}

void UQuestTriggerComponent::OnTriggerStepCompleted(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
    OnTriggerStepEnded(Channel);
}

void UQuestTriggerComponent::OnTriggerStepDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    OnTriggerStepEnded(Channel);
}

void UQuestTriggerComponent::OnTriggerStepEnded(FGameplayTag Channel)
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

    UE_LOG(LogSimpleQuestSubscription, VeryVerbose, TEXT("UQuestTriggerComponent::OnTriggerStepEnded : Channel: %s : Owner: %s"),
        *Channel.ToString(),
        *GetOwner()->GetClass()->GetFName().ToString());

    // Only visually deactivate when no other watched steps remain active. Both subscription maps are kept in
    // sync (each (Channel, end-event-type) pair adds and removes together), so either map's emptiness is a
    // sufficient check — but using ActiveStepEndHandles keeps a single canonical source of truth.
    if (ActiveStepEndHandles.IsEmpty())
    {
        SetActivated(false);
    }
}

int32 UQuestTriggerComponent::RemoveTags(const TArray<FGameplayTag>& TagsToRemove)
{
    int32 Count = 0;
    for (const FGameplayTag& Tag : TagsToRemove)
    {
        if (StepTagsToTrigger.HasTagExact(Tag))
        {
            if (Count == 0) Modify();
            StepTagsToTrigger.RemoveTag(Tag);
            ++Count;
        }
    }
    if (Count > 0 && GetOwner())
    {
        GetOwner()->MarkPackageDirty();
    }
    return Count;
}

TArray<FQuestObservedTagSpec> UQuestTriggerComponent::GetImplicitlyObservedTags() const
{
    TArray<FQuestObservedTagSpec> Implicit = Super::GetImplicitlyObservedTags();
    Implicit.Reserve(Implicit.Num() + StepTagsToTrigger.Num());
    for (const FGameplayTag& Tag : StepTagsToTrigger)
    {
        // Trigger keeps default routing for now — Step tags subscribed hierarchically preserve current
        // behavior. Audit (TODO §4.38) classifies whether Trigger should narrow to ExactMatch in a future pass.
        Implicit.Add(FQuestObservedTagSpec{Tag, SignalRoutingDefaults::HierarchicalSubscribe});
    }
    return Implicit;
}

void UQuestTriggerComponent::SetActivated_Implementation(bool bIsActivated)
{
    OnQuestTriggerActivated.Broadcast(bIsActivated);
}

void UQuestTriggerComponent::SendTriggerEvent(const FQuestObjectiveTriggerContext& Context)
{
    if (!SignalSubsystem) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestTriggerComponent_SendTriggerEvent);

    // Default TriggeredActor to the owning actor when the caller didn't supply one. "What was triggered" is almost
    // always this trigger's owner — most external callers won't bother to set it explicitly.
    AActor* OwnerActor = GetOwner();
    UObject* TriggeredActor = Context.TriggeredActor ? Context.TriggeredActor.Get() : Cast<UObject>(OwnerActor);
    UObject* Instigator = Context.Instigator.IsValid() ? Context.Instigator.Get() : nullptr;

    // Echo context for Blocked publishes — TriggeredActor populated as a convenience; OriginatingTriggerComponent
    // is the framework-owned identity subscriber-side filters key on. The Live-path uses FQuestTriggerFiredEvent
    // which takes the raw fields directly.
    FQuestObjectiveTriggerContext EchoContext = Context;
    if (!EchoContext.TriggeredActor) EchoContext.TriggeredActor = OwnerActor;
    EchoContext.OriginatingTriggerComponent = this;

    UQuestStateSubsystem* StateSubsystem = GetWorld() && GetWorld()->GetGameInstance()
        ? GetWorld()->GetGameInstance()->GetSubsystem<UQuestStateSubsystem>()
        : nullptr;

    const FGameplayTagContainer Registered = GetRegisteredStepTagsToTrigger();
    UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("UQuestTriggerComponent::SendTriggerEvent : '%s' fired by '%s' against %d watched step(s); CustomData %s"),
        TriggeredActor ? *TriggeredActor->GetName() : TEXT("(none)"),
        Instigator ? *Instigator->GetName() : TEXT("(none)"),
        Registered.Num(), Context.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));

    for (const FGameplayTag& StepTag : Registered)
    {
        // Branch per-step on state. Use the activation-blocker query as the single source of truth: AlreadyLive
        // in the result means route to objective for per-fire Response; Blocked / PrereqUnmet (without AlreadyLive)
        // means publish Blocked for adopter UI feedback; anything else (Idle, Completed, unknown) means silent —
        // a trigger fire against a step the runtime hasn't activated isn't the trigger's concern.
        TArray<FQuestActivationBlocker> Blockers;
        if (StateSubsystem) Blockers = StateSubsystem->QueryQuestActivationBlockers(StepTag);

        bool bIsLive = false;
        for (const FQuestActivationBlocker& Blocker : Blockers)
        {
            if (Blocker.Reason == EQuestActivationBlocker::AlreadyLive) { bIsLive = true; break; }
        }

        // Resolve watched-tag perspective to canonical + all aliases. The designer's authored StepTagsToTrigger may
        // hold any perspective form (canonical on standalone content; alias on LinkedQuestline content); the
        // manager's FQuestTriggerFiredEvent subscription installs on canonical (Node->GetContextualTag()), and other
        // Trigger Components watching the same step may bind on different perspectives. Multi-publish on the full
        // channel set matches the FQuestPublish::OnAllNodeTags model so any-perspective subscriber receives via the
        // bus's per-subscription dedup.
        TArray<FGameplayTag> Channels;
        if (StateSubsystem)
        {
            for (const FGameplayTag& Canonical : StateSubsystem->ResolveCanonicalTags(StepTag))
            {
                Channels.AddUnique(Canonical);
                for (const FGameplayTag& Alias : StateSubsystem->GetAssetScopedAliasTagsForCanonical(Canonical))
                {
                    Channels.AddUnique(Alias);
                }
            }
        }
        if (Channels.IsEmpty()) Channels.Add(StepTag);

        if (bIsLive)
        {
            SignalSubsystem->PublishMessageOnChannels(MoveTemp(Channels), FQuestTriggerFiredEvent(TriggeredActor, Instigator, Context.CustomData, this));
            continue;
        }

        // Filter to structural blockers — the user-actionable subset. Other reasons (NotPendingGiver, UnknownQuest)
        // mean the step isn't yet activated; trigger fires against those aren't surfaced.
        TArray<FQuestActivationBlocker> StructuralBlockers = Blockers.FilterByPredicate([](const FQuestActivationBlocker& B)
        {
            return B.Reason == EQuestActivationBlocker::Blocked || B.Reason == EQuestActivationBlocker::PrereqUnmet;
        });

        if (!StructuralBlockers.IsEmpty())
        {
            // Canonical identity for the event payload — first channel in the set after resolve, matches
            // FQuestPublish::OnAllNodeTags semantics where Event.QuestTag is set to the canonical ContextualTag.
            const FGameplayTag IdentityTag = Channels[0];
            SignalSubsystem->PublishMessageOnChannels(MoveTemp(Channels), FQuestTriggerBlockedEvent(IdentityTag, StructuralBlockers, EchoContext));
        }
    }
}

void UQuestTriggerComponent::HandleQuestTriggerResponse(FGameplayTag Channel, const FQuestTriggerResponseEvent& Event)
{
    if (Event.TriggerContext.OriginatingTriggerComponent.Get() != this) return;

    UE_LOG(LogSimpleQuestSubscription, VeryVerbose, TEXT("UQuestTriggerComponent::HandleQuestTriggerResponse : '%s' resolution=%d outcome='%s' refusal='%s'"),
        *Event.QuestTag.ToString(),
        static_cast<int32>(Event.Resolution),
        *Event.OutcomeTag.ToString(),
        *Event.RefusalReason.ToString());

    OnQuestTriggerResponded.Broadcast(Event.QuestTag, Channel, Event);
}

void UQuestTriggerComponent::HandleQuestTriggerBlocked(FGameplayTag Channel, const FQuestTriggerBlockedEvent& Event)
{
    if (Event.TriggerContext.OriginatingTriggerComponent.Get() != this) return;

    UE_LOG(LogSimpleQuestSubscription, VeryVerbose, TEXT("UQuestTriggerComponent::HandleQuestTriggerBlocked : '%s' %d blocker(s)"),
        *Event.QuestTag.ToString(), Event.Blockers.Num());

    OnQuestTriggerBlocked.Broadcast(Event.QuestTag, Channel, Event);
}

void UQuestTriggerComponent::HandleQuestTriggerDeactivated(FGameplayTag Channel, const FQuestTriggerDeactivatedEvent& Event)
{
    UE_LOG(LogSimpleQuestSubscription, VeryVerbose, TEXT("UQuestTriggerComponent::HandleQuestTriggerDeactivated : '%s' reason=%d outcome='%s'"),
        *Event.QuestTag.ToString(),
        static_cast<int32>(Event.EndReason),
        *Event.OutcomeTag.ToString());

    OnQuestTriggerDeactivated.Broadcast(Event.QuestTag, Channel, Event);
}

FGameplayTagContainer UQuestTriggerComponent::GetRegisteredStepTagsToTrigger() const
{
    return FQuestTagComposer::FilterToRegisteredTags(
        StepTagsToTrigger,
        FString::Printf(TEXT("UQuestTriggerComponent::GetRegisteredStepTagsToTrigger ('%s')"),
            GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

