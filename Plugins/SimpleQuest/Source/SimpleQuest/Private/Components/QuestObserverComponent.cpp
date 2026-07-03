// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#include "Components/QuestObserverComponent.h"

#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestActivationFailedEvent.h"
#include "Events/QuestBlockedEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestDisabledEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestProgressEvent.h"
#include "Events/QuestProgressRefusedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestUnblockedEvent.h"
#include "Quests/Types/QuestObservedTagSpec.h"
#include "Subsystems/SignalSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Utilities/QuestCatchUpFanout.h"
#include "Utilities/QuestTagComposer.h"
#include "Utilities/SignalChannelUtils.h"
#include "Subsystems/WorldStateSubsystem.h"


UQuestObserverComponent::UQuestObserverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestObserverComponent::BeginPlay()
{
	Super::BeginPlay();

	// Defer registration and catch-up to the next tick. Component BeginPlay runs before the owning actor's Event
	// BeginPlay (where it creates state and binds our delegates), so registering synchronously here would deliver
	// catch-up — and any same-frame live event — to a half-initialized owner. An actor's whole BeginPlay is
	// synchronous within the frame, so one tick is guaranteed to land after it. RegisterQuestObserver stays public
	// for the rare owner that finishes setup across multiple frames and wants to register explicitly.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UQuestObserverComponent::PerformDeferredRegistration);
	}
}

void UQuestObserverComponent::PerformDeferredRegistration()
{
	RegisterQuestObserver();
}

void UQuestObserverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Cancel a still-pending deferred registration so it can't fire onto a torn-down component.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	// Bulk-clear every channel subscription this component (and any derived subclass — Trigger, Giver) registered with the bus.
	if (SignalSubsystem)
	{
		SignalSubsystem->UnsubscribeListener(this);
	}
	// Strip every per-role source-registry entry pointing at this component. Weak pointers would auto-skip on query, but
	// explicit removal keeps the registry compact across repeated activate/end cycles (PIE bounce, level-streaming).
	// Single call covers all three role maps.
	if (const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UQuestStateSubsystem* StateSubsystem = GI->GetSubsystem<UQuestStateSubsystem>())
		{
			StateSubsystem->UnregisterAllRoleSources(this);
		}
	}
	SubscriptionHandlesByTag.Empty();
	bRegistered = false;
	Super::EndPlay(EndPlayReason);
}

void UQuestObserverComponent::HandleQuestActivated(FGameplayTag Channel, const FQuestActivatedEvent& Event)
{
	if (OnQuestActivated.IsBound())
	{
		OnQuestActivated.Broadcast(Event.GetQuestTag(), Channel, Event.Payload, Event.PrereqStatus);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Activated, Event.Payload);
}

void UQuestObserverComponent::HandleQuestActivationFailed(FGameplayTag Channel, const FQuestActivationFailedEvent& Event)
{
	if (OnQuestActivationFailed.IsBound())
	{
		OnQuestActivationFailed.Broadcast(Event.GetQuestTag(), Event.AttemptedTagName, Channel, Event.Reason, Event.Payload);
	}
}

void UQuestObserverComponent::HandleQuestEnabled(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
	ActiveQuestTags.AddTag(Event.GetQuestTag());
	if (OnQuestEnabled.IsBound())
	{
		OnQuestEnabled.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Enabled, Event.Payload);
}

void UQuestObserverComponent::HandleQuestDisabled(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
	if (OnQuestDisabled.IsBound())
	{
		OnQuestDisabled.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Disabled, Event.Payload);
}

void UQuestObserverComponent::HandleQuestGiveBlocked(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
	if (OnQuestGiveBlocked.IsBound())
	{
		OnQuestGiveBlocked.Broadcast(Event.GetQuestTag(), Channel, Event.Blockers, Event.GiverActor.Get());
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::GiveBlocked, Event.Payload, FGameplayTag(), Event.GiverActor.Get());
}

void UQuestObserverComponent::HandleQuestStarted(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
	if (OnQuestStarted.IsBound())
	{
		OnQuestStarted.Broadcast(Event.GetQuestTag(), Channel, Event.Payload, Event.GiverActor.Get());
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Started, Event.Payload, FGameplayTag(), Event.GiverActor.Get());
}

void UQuestObserverComponent::HandleQuestProgress(FGameplayTag Channel, const FQuestProgressEvent& Event)
{
	if (OnQuestProgress.IsBound())
	{
		OnQuestProgress.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Progress, Event.Payload);
}

void UQuestObserverComponent::HandleQuestCompleted(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
	ActiveQuestTags.RemoveTag(Event.GetQuestTag());
	CompletedQuestTags.AddTag(Event.GetQuestTag());

	// Find the most-specific watched entry whose key is an ancestor of (or equals) Channel — that's the
	// authored binding this delivery corresponds to. Direct ObservedTags.Find(Channel) was the prior shape,
	// which silently bypassed the outcome filter for parent-prefix subscriptions: a observer authored at
	// SimpleQuest.Questline.MyLine receiving an event published on SimpleQuest.Questline.MyLine.Step1 has Channel
	// = the descendant, but ObservedTags is keyed by the authored ancestor — direct lookup returned null
	// and the filter never applied. Walk the entries instead, picking the longest matching ancestor (most
	// specific authored binding wins when multiple match — typical case is one authored binding per event).
	const FObservedQuestEventSettings* MatchingSettings = nullptr;
	int32 BestKeyDepth = -1;
	for (const TPair<FGameplayTag, FObservedQuestEventSettings>& Pair : ObservedTags)
	{
		if (!Channel.MatchesTag(Pair.Key)) continue;  // Pair.Key must be ancestor of (or equal) Channel

		int32 KeyDepth = 0;
		FGameplayTag Walker = Pair.Key;
		while (Walker.IsValid())
		{
			++KeyDepth;
			Walker = Walker.RequestDirectParent();
		}
		if (KeyDepth > BestKeyDepth)
		{
			MatchingSettings = &Pair.Value;
			BestKeyDepth = KeyDepth;
		}
	}

	// Apply outcome filter from the most-specific matching authored binding. If no entries match (defensive —
	// shouldn't happen since this callback only fires for subscriptions made from ObservedTags), fall through
	// to broadcast unfiltered.
	if (MatchingSettings && !MatchingSettings->OutcomeFilter.IsEmpty()
		&& !MatchingSettings->OutcomeFilter.HasTagExact(Event.OutcomeTag))
	{
		UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("QuestObserver: quest '%s' completed with outcome '%s' — filtered out, skipping broadcast"),
			*Event.GetQuestTag().ToString(),
			*Event.OutcomeTag.ToString());
		return;
	}

	if (OnQuestCompleted.IsBound())
	{
		OnQuestCompleted.Broadcast(Event.GetQuestTag(), Channel, Event.OutcomeTag, Event.Payload);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Completed, Event.Payload, Event.OutcomeTag);
}

void UQuestObserverComponent::HandleQuestDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
	ActiveQuestTags.RemoveTag(Event.GetQuestTag());
	if (OnQuestDeactivated.IsBound())
	{
		OnQuestDeactivated.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Deactivated, Event.Payload);
}

void UQuestObserverComponent::HandleQuestBlocked(FGameplayTag Channel, const FQuestBlockedEvent& Event)
{
	if (OnQuestBlocked.IsBound())
	{
		OnQuestBlocked.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Blocked, Event.Payload);
}

void UQuestObserverComponent::HandleQuestUnblocked(FGameplayTag Channel, const FQuestUnblockedEvent& Event)
{
	if (OnQuestUnblocked.IsBound())
	{
		OnQuestUnblocked.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::Unblocked, Event.Payload);
}

void UQuestObserverComponent::HandleQuestProgressRefused(FGameplayTag Channel, const FQuestProgressRefusedEvent& Event)
{
	if (OnQuestProgressRefused.IsBound())
	{
		OnQuestProgressRefused.Broadcast(Event.GetQuestTag(), Channel, Event.Blockers, Event.TriggerContext);
	}
	BroadcastAnyQuestEvent(Event.GetQuestTag(), Channel, EQuestLifecycleEventType::ProgressRefused, Event.Payload, FGameplayTag(), Event.TriggerContext.TriggeredActor.Get());
}

void UQuestObserverComponent::BroadcastAnyQuestEvent(FGameplayTag QuestTag, FGameplayTag MatchedChannel, EQuestLifecycleEventType EventType, const FQuestEventPayload& Payload, FGameplayTag OutcomeTag, AActor* GiverActor)
{
	if (!OnAnyQuestEvent.IsBound()) return;
	FQuestLifecycleEventReport Report;
	Report.QuestTag = QuestTag;
	Report.MatchedChannel = MatchedChannel;
	Report.EventType = EventType;
	Report.Payload = Payload;
	Report.OutcomeTag = OutcomeTag;
	Report.GiverActor = GiverActor;
	OnAnyQuestEvent.Broadcast(Report);
}

int32 UQuestObserverComponent::ApplyTagRenames(const TMap<FName, FName>& Renames)
{
	// Specialty handling for ObservedTags only. This TMap has FGameplayTag KEYS, which the editor-side reflection
	// sweep can't address — TMap doesn't permit in-place key mutation, so the rewrite is remove-then-readd. The
	// generic FGameplayTagContainer field (WatchedStepTags) is handled by the reflection sweep in the loader; this
	// override adds only what reflection can't reach.
	int32 Count = 0;
	for (const auto& [OldName, NewName] : Renames)
	{
		FGameplayTag FoundMapKey;
		for (const auto& [Key, Value] : ObservedTags)
		{
			if (Key.GetTagName() == OldName)
			{
				FoundMapKey = Key;
				break;
			}
		}
		if (FoundMapKey.IsValid())
		{
			FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(NewName, false);
			if (NewTag.IsValid())
			{
				FObservedQuestEventSettings Moved = MoveTemp(ObservedTags[FoundMapKey]);
				ObservedTags.Remove(FoundMapKey);
				ObservedTags.Add(NewTag, MoveTemp(Moved));
				Count++;
			}
		}
	}
	return Count;
}

int32 UQuestObserverComponent::RemoveTags(const TArray<FGameplayTag>& TagsToRemove)
{
	int32 Count = 0;
	for (const FGameplayTag& Tag : TagsToRemove)
	{
		if (WatchedStepTags.HasTagExact(Tag))
		{
			if (Count == 0) Modify();
			WatchedStepTags.RemoveTag(Tag);
			++Count;
		}
		const int32 MapRemoved = ObservedTags.Remove(Tag);
		if (MapRemoved > 0)
		{
			if (Count == 0) Modify();
			Count += MapRemoved;
		}
	}
	if (Count > 0 && GetOwner())
	{
		GetOwner()->MarkPackageDirty();
	}
	return Count;
}

void UQuestObserverComponent::RegisterQuestObserver()
{
	if (!SignalSubsystem)
	{
		UE_LOG(LogSimpleQuestSubscription, Error, TEXT("UQuestObserverComponent::RegisterQuestObserver : QuestSignalSubsystem is null, aborting."));
		return;
	}

	// Build the effective observed set:
	//   - Start with the designer-authored ObservedTags.
	//   - For each implicit-observed tag (Trigger's StepTagsToTrigger, Giver's QuestTagsToGive, etc.),
	//     either use the existing designer-authored entry OR create a fresh entry with implicit-default
	//     flag overlay.
	//   - Force-on the give-flow pair (bObserveStarted + bObserveGiveBlocked) on EVERY implicit-observed
	//     tag regardless of source — these protect success/refusal symmetry and override designer config
	//     silencing.
	//   - Apply implicit defaults (bObserveProgress / bObserveBlocked / bObserveUnblocked) ONLY on fresh
	//     entries — Progress for run-phase UI auto-binding, Blocked/Unblocked as a symmetric pair for
	//     block-state UI. Designer-authored entries keep their authored flag values for these.
	TMap<FGameplayTag, FObservedQuestEventSettings> EffectiveObserved = ObservedTags;
	for (const FQuestObservedTagSpec& Spec : GetImplicitlyObservedTags())
	{
		if (!Spec.Tag.IsValid()) continue;

		const bool bDesignerAuthored = EffectiveObserved.Contains(Spec.Tag);
		FObservedQuestEventSettings& Settings = EffectiveObserved.FindOrAdd(Spec.Tag);

		if (!bDesignerAuthored)
		{
			// Implicit-only defaults — ergonomic flags that auto-bind for derived-component managed tags.
			Settings.bObserveProgress = true;
			Settings.bObserveBlocked = true;
			Settings.bObserveUnblocked = true;

			// Routing comes from the bridge owner — designer-authored entries keep their authored Routing.
			Settings.Routing = Spec.Routing;
		}

		// Force-on the give-flow invariant pair regardless of source — silencing either half breaks the
		// success/refusal observability symmetry.
		Settings.bObserveStarted = true;
		Settings.bObserveGiveBlocked = true;
	}

	// The deferred registration pass has run — flip the flag BEFORE the empty-set early-out so a component that
	// starts with no tags (the runtime-AddObservedTag case) still counts as registered. Without this, AddObservedTag's
	// bRegistered guard never trips and runtime adds silently skip the live subscribe + catch-up.
	bRegistered = true;

	if (EffectiveObserved.IsEmpty())
	{
		if (GetOwner())
		{
			UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("UQuestObserverComponent::RegisterQuestObserver : no observed tags at registration (valid for runtime-driven sets). Actor: %s"), *GetOwner()->GetActorNameOrLabel());
		}
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UQuestObserverComponent_RegisterQuestObserver);

	for (const TPair<FGameplayTag, FObservedQuestEventSettings>& Pair : EffectiveObserved)
	{
		RegisterSingleObservedTag(Pair.Key, Pair.Value);
	}
}

void UQuestObserverComponent::AddObservedTag(FGameplayTag QuestTag, FObservedQuestEventSettings Settings)
{
	if (!QuestTag.IsValid()) return;

	ObservedTags.Add(QuestTag, Settings);  // TMap::Add overwrites — updates settings if the tag was already present

	if (bRegistered)
	{
		// If this tag is already live, drop its old subscriptions first so we neither double-subscribe nor strand
		// stale Settings, then (re-)subscribe + catch up with the current settings.
		if (SubscriptionHandlesByTag.Contains(QuestTag))
		{
			UnregisterSingleObservedTag(QuestTag);
		}
		RegisterSingleObservedTag(QuestTag, Settings);
	}
}

void UQuestObserverComponent::RemoveObservedTag(FGameplayTag QuestTag)
{
	ObservedTags.Remove(QuestTag);

	if (bRegistered)
	{
		UnregisterSingleObservedTag(QuestTag);
	}
}

void UQuestObserverComponent::RegisterSingleObservedTag(const FGameplayTag& QuestTag, const FObservedQuestEventSettings& Settings)
{
	if (!SignalSubsystem || !QuestTag.IsValid()) return;

	if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
	{
		UE_LOG(LogSimpleQuestSubscription, Warning,
			TEXT("UQuestObserverComponent::RegisterSingleObservedTag : '%s' holds stale tag '%s' — skipping subscribe. ")
			TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown"), *QuestTag.ToString());
		return;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UWorldStateSubsystem* WorldState = GameInstance ? GameInstance->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	UQuestStateSubsystem* StateSubsystem = GameInstance ? GameInstance->GetSubsystem<UQuestStateSubsystem>() : nullptr;

	// Per-tag observer-source registration (additive — RegisterRoleSource only touches this tag's bucket).
	if (StateSubsystem)
	{
		StateSubsystem->RegisterObserverSource(this, FGameplayTagContainer(QuestTag));
	}

	// Live subscriptions — one per opted-in event type — capturing each handle for selective unsubscribe later.
	TArray<FDelegateHandle>& Handles = SubscriptionHandlesByTag.FindOrAdd(QuestTag);
	if (Settings.bObserveActivated)        Handles.Add(SignalSubsystem->SubscribeMessage<FQuestActivatedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestActivated, Settings.Routing));
	if (Settings.bObserveActivationFailed) Handles.Add(SignalSubsystem->SubscribeMessage<FQuestActivationFailedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestActivationFailed, Settings.Routing));
	if (Settings.bObserveEnabled)          Handles.Add(SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestEnabled, Settings.Routing));
	if (Settings.bObserveDisabled)         Handles.Add(SignalSubsystem->SubscribeMessage<FQuestDisabledEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestDisabled, Settings.Routing));
	if (Settings.bObserveGiveBlocked)      Handles.Add(SignalSubsystem->SubscribeMessage<FQuestGiveBlockedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestGiveBlocked, Settings.Routing));
	if (Settings.bObserveStarted)          Handles.Add(SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestStarted, Settings.Routing));
	if (Settings.bObserveProgress)         Handles.Add(SignalSubsystem->SubscribeMessage<FQuestProgressEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestProgress, Settings.Routing));
	if (Settings.bObserveCompleted)        Handles.Add(SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestCompleted, Settings.Routing));
	if (Settings.bObserveDeactivated)      Handles.Add(SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestDeactivated, Settings.Routing));
	if (Settings.bObserveBlocked)          Handles.Add(SignalSubsystem->SubscribeMessage<FQuestBlockedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestBlocked, Settings.Routing));
	if (Settings.bObserveUnblocked)        Handles.Add(SignalSubsystem->SubscribeMessage<FQuestUnblockedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestUnblocked, Settings.Routing));
	if (Settings.bObserveProgressRefused)  Handles.Add(SignalSubsystem->SubscribeMessage<FQuestProgressRefusedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestProgressRefused, Settings.Routing));

	CatchUpSingleTag(QuestTag, Settings, WorldState, StateSubsystem);
}

void UQuestObserverComponent::CatchUpSingleTag(const FGameplayTag& QuestTag, const FObservedQuestEventSettings& Settings, UWorldStateSubsystem* WorldState, UQuestStateSubsystem* QuestState)
{
	if (!WorldState) return;

	// Catch-up: fire delegates immediately for state already present at subscription time. For an exact-tag
	// observer this fires per-pin if a matching state fact is set for QuestTag itself; for a parent-prefix
	// observer (subscribed tag is an unknown namespace OR a known wrapper container) it fans out to every
	// known descendant via FQuestCatchUpFanout and probes each one in turn, mirroring the signal bus's
	// hierarchical broadcast on the live side. Synthetic Payload carries each descendant's tag only — full
	// Payload isn't recoverable from state alone (NodeInfo display fields, CompletionTrigger, and the
	// inherited FQuestContextBase attribution come from runtime publish-time AssembleEventContext calls;
	// Started catch-up additionally recovers the last giver actor via StateSubsystem). Mirrors UQuestEvent-
	// Subscription's catch-up pattern.
	//
	// No per-tag deduplication against live events here (unlike UQuestLifecycleObserver): subscription and catch-up
	// happen together in this call, so there's no window where the component is subscribed but not yet caught up —
	// a live event can't slip in mid-pass and need deduplication. This call is itself deferred one tick past BeginPlay
	// (PerformDeferredRegistration) so the owning actor initializes first, but subscribe + catch-up stay atomic within it.
	const TArray<FGameplayTag> CatchUpTags = FQuestCatchUpFanout::EnumerateTagsForCatchUp(QuestTag, QuestState, Settings.Routing);

	for (const FGameplayTag& EachTag : CatchUpTags)
	{
		// EachTag is the canonical for this catch-up entry (post-GetQuestTagsUnderPrefix's alias-resolution).
		// Build the channel set [canonical, ...aliases] and pick the best match for this watched-key tag —
		// same selection the live bus dispatcher uses, so observer delegates see consistent MatchedChannel
		// values across catch-up and live deliveries (no need to branch by delivery path).
		TArray<FGameplayTag> ChannelSet;
		ChannelSet.Add(EachTag);
		if (QuestState)
		{
			for (const FGameplayTag& AliasTag : QuestState->GetAssetScopedAliasTagsForCanonical(EachTag))
			{
				ChannelSet.Add(AliasTag);
			}
		}
		const FGameplayTag MatchedChannel = FSignalChannelUtils::PickBestMatchChannel(ChannelSet, QuestTag);

		FQuestEventPayload Payload;
		Payload.NodeInfo.QuestTag = EachTag;
		Payload.Delivery = EQuestEventDelivery::CatchUp;   // every broadcast below is a reconstruction, not a live transition

		// Activated / Started reconstruct from any fact that proves the quest ENTERED SCOPE — PendingGiver (giver-gated
		// path), Live (non-giver path went straight to Live), OR the append-only Completed anchor (a finished quest
		// necessarily activated and went live to reach a resolved state; Live/PendingGiver are transient and already
		// cleared). Without the Completed branch a finished quest replays only its Completed event, so presentation
		// keyed on Activated/Started — a door that opens when its chapter is reached — never reconstructs for completed
		// content. Enabled stays PendingGiver-only. Matches UQuestLifecycleObserver's catch-up so the component and the
		// K2 node replay identically.
		const FGameplayTag PendingFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::PendingGiver);
		const bool bIsPendingGiver = PendingFact.IsValid() && WorldState->HasFact(PendingFact);

		const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Live);
		const bool bIsLive = LiveFact.IsValid() && WorldState->HasFact(LiveFact);

		const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Completed);
		const bool bIsCompleted = CompletedFact.IsValid() && WorldState->HasFact(CompletedFact);

		FQuestPrereqStatus CachedPrereqStatus;
		if (bIsPendingGiver && QuestState)
		{
			CachedPrereqStatus = QuestState->GetQuestPrereqStatus(EachTag);
		}

		if (Settings.bObserveActivated && (bIsPendingGiver || bIsLive || bIsCompleted))
		{
			ActiveQuestTags.AddTag(EachTag);
			if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(EachTag, MatchedChannel, Payload, CachedPrereqStatus);
			BroadcastAnyQuestEvent(EachTag, MatchedChannel, EQuestLifecycleEventType::Activated, Payload);
		}

		if (Settings.bObserveEnabled && bIsPendingGiver && CachedPrereqStatus.bSatisfied)
		{
			ActiveQuestTags.AddTag(EachTag);
			if (OnQuestEnabled.IsBound()) OnQuestEnabled.Broadcast(EachTag, MatchedChannel, Payload);
			BroadcastAnyQuestEvent(EachTag, MatchedChannel, EQuestLifecycleEventType::Enabled, Payload);
		}

		if (Settings.bObserveStarted && (bIsLive || bIsCompleted))
		{
			ActiveQuestTags.AddTag(EachTag);
			AActor* RecoveredGiver = QuestState ? QuestState->GetLastGiverActor(EachTag) : nullptr;
			if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(EachTag, MatchedChannel, Payload, RecoveredGiver);
			BroadcastAnyQuestEvent(EachTag, MatchedChannel, EQuestLifecycleEventType::Started, Payload, FGameplayTag(), RecoveredGiver);
		}

		// Disabled / GiveBlocked / Progress / Unblocked / ProgressRefused have no catch-up — transient or
		// one-shot events without recoverable state.

		if (Settings.bObserveCompleted && bIsCompleted)
		{
			ActiveQuestTags.RemoveTag(EachTag);
			CompletedQuestTags.AddTag(EachTag);

			FGameplayTag RecoveredOutcome = FGameplayTag::EmptyTag;
			if (QuestState)
			{
				if (const FQuestResolutionRecord* Record = QuestState->GetQuestResolution(EachTag))
				{
					if (const FQuestResolutionEntry* Latest = Record->GetLatest())
					{
						RecoveredOutcome = Latest->OutcomeTag;
					}
				}
			}
				
			if (!Settings.OutcomeFilter.IsEmpty() && !Settings.OutcomeFilter.HasTagExact(RecoveredOutcome))
			{
				UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("QuestObserver: catch-up for '%s' recovered outcome '%s' — filtered out, skipping broadcast"),
					*EachTag.ToString(), *RecoveredOutcome.ToString());
			}
			else
			{
				UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("QuestObserver: catch-up for '%s' — recovered outcome '%s' from registry"),
					*EachTag.ToString(), *RecoveredOutcome.ToString());
				if (OnQuestCompleted.IsBound()) OnQuestCompleted.Broadcast(EachTag, MatchedChannel, RecoveredOutcome, Payload);
				BroadcastAnyQuestEvent(EachTag, MatchedChannel, EQuestLifecycleEventType::Completed, Payload, RecoveredOutcome);
			}
		}

		if (Settings.bObserveDeactivated)
		{
			const FGameplayTag DeactivatedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Deactivated);
			if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
			{
				ActiveQuestTags.RemoveTag(EachTag);
				if (OnQuestDeactivated.IsBound()) OnQuestDeactivated.Broadcast(EachTag, MatchedChannel, Payload);
				BroadcastAnyQuestEvent(EachTag, MatchedChannel, EQuestLifecycleEventType::Deactivated, Payload);
			}
		}

		if (Settings.bObserveBlocked)
		{
			const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Blocked);
			if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
			{
				if (OnQuestBlocked.IsBound()) OnQuestBlocked.Broadcast(EachTag, MatchedChannel, Payload);
				BroadcastAnyQuestEvent(EachTag, MatchedChannel, EQuestLifecycleEventType::Blocked, Payload);
			}
		}
	}
}

void UQuestObserverComponent::UnregisterSingleObservedTag(const FGameplayTag& QuestTag)
{
	if (SignalSubsystem)
	{
		if (const TArray<FDelegateHandle>* Handles = SubscriptionHandlesByTag.Find(QuestTag))
		{
			for (const FDelegateHandle& Handle : *Handles)
			{
				SignalSubsystem->UnsubscribeMessage(QuestTag, Handle);
			}
		}
	}
	SubscriptionHandlesByTag.Remove(QuestTag);

	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UQuestStateSubsystem* StateSubsystem = GameInstance->GetSubsystem<UQuestStateSubsystem>())
		{
			StateSubsystem->UnregisterObserverSource(this, QuestTag);
		}
	}

	ActiveQuestTags.RemoveTag(QuestTag);
	CompletedQuestTags.RemoveTag(QuestTag);
}

FGameplayTagContainer UQuestObserverComponent::GetRegisteredWatchedStepTags() const
{
	return FQuestTagComposer::FilterToRegisteredTags(
		WatchedStepTags,
		FString::Printf(TEXT("UQuestObserverComponent::GetRegisteredWatchedStepTags ('%s')"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

FGameplayTagContainer UQuestObserverComponent::GetRegisteredWatchedQuestKeys() const
{
	FGameplayTagContainer KeysContainer;
	for (const auto& Pair : ObservedTags) KeysContainer.AddTag(Pair.Key);
	return FQuestTagComposer::FilterToRegisteredTags(
		KeysContainer,
		FString::Printf(TEXT("UQuestObserverComponent::GetRegisteredWatchedQuestKeys ('%s')"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

