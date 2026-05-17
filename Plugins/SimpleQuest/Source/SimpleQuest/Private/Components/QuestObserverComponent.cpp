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
#include "Events/QuestStartedEvent.h"
#include "Events/QuestUnblockedEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Utilities/QuestCatchUpFanout.h"
#include "Utilities/QuestTagComposer.h"
#include "Utilities/SignalChannelUtils.h"
#include "WorldState/WorldStateSubsystem.h"


UQuestObserverComponent::UQuestObserverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestObserverComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterQuestObserver();
}

void UQuestObserverComponent::HandleQuestActivated(FGameplayTag Channel, const FQuestActivatedEvent& Event)
{
	if (OnQuestActivated.IsBound())
	{
		OnQuestActivated.Broadcast(Event.GetQuestTag(), Channel, Event.Payload, Event.PrereqStatus);
	}
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
}

void UQuestObserverComponent::HandleQuestDisabled(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
	if (OnQuestDisabled.IsBound())
	{
		OnQuestDisabled.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
}

void UQuestObserverComponent::HandleQuestGiveBlocked(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
	if (OnQuestGiveBlocked.IsBound())
	{
		OnQuestGiveBlocked.Broadcast(Event.GetQuestTag(), Channel, Event.Blockers, Event.GiverActor.Get());
	}
}

void UQuestObserverComponent::HandleQuestStarted(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
	if (OnQuestStarted.IsBound())
	{
		OnQuestStarted.Broadcast(Event.GetQuestTag(), Channel, Event.Payload, Event.GiverActor.Get());
	}
}

void UQuestObserverComponent::HandleQuestProgress(FGameplayTag Channel, const FQuestProgressEvent& Event)
{
	if (OnQuestProgress.IsBound())
	{
		OnQuestProgress.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
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
}

void UQuestObserverComponent::HandleQuestDeactivated(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
	ActiveQuestTags.RemoveTag(Event.GetQuestTag());
	if (OnQuestDeactivated.IsBound())
	{
		OnQuestDeactivated.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
}

void UQuestObserverComponent::HandleQuestBlocked(FGameplayTag Channel, const FQuestBlockedEvent& Event)
{
	if (OnQuestBlocked.IsBound())
	{
		OnQuestBlocked.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
}

void UQuestObserverComponent::HandleQuestUnblocked(FGameplayTag Channel, const FQuestUnblockedEvent& Event)
{
	if (OnQuestUnblocked.IsBound())
	{
		OnQuestUnblocked.Broadcast(Event.GetQuestTag(), Channel, Event.Payload);
	}
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
	for (const FGameplayTag& ImplicitTag : GetImplicitlyObservedTags())
	{
		if (!ImplicitTag.IsValid()) continue;

		const bool bDesignerAuthored = EffectiveObserved.Contains(ImplicitTag);
		FObservedQuestEventSettings& Settings = EffectiveObserved.FindOrAdd(ImplicitTag);

		if (!bDesignerAuthored)
		{
			// Implicit-only defaults — ergonomic flags that auto-bind for derived-component managed tags.
			Settings.bObserveProgress = true;
			Settings.bObserveBlocked = true;
			Settings.bObserveUnblocked = true;
		}

		// Force-on the give-flow invariant pair regardless of source — silencing either half breaks the
		// success/refusal observability symmetry.
		Settings.bObserveStarted = true;
		Settings.bObserveGiveBlocked = true;
	}

	if (EffectiveObserved.IsEmpty())
	{
		if (GetOwner())
		{
			UE_LOG(LogSimpleQuestSubscription, Warning, TEXT("UQuestObserverComponent::RegisterQuestObserver : no observed tags resolved. Actor: %s"), *GetOwner()->GetActorNameOrLabel());
		}
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UQuestObserverComponent_RegisterQuestObserver);

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UWorldStateSubsystem* WorldState = GameInstance ? GameInstance->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	UQuestStateSubsystem* StateSubsystem = GameInstance ? GameInstance->GetSubsystem<UQuestStateSubsystem>() : nullptr;

	for (auto& QuestPair : EffectiveObserved)
	{
		const FGameplayTag& QuestTag = QuestPair.Key;
		const FObservedQuestEventSettings& Settings = QuestPair.Value;

		if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
		{
			UE_LOG(LogSimpleQuestSubscription, Warning,
				TEXT("UQuestObserverComponent::RegisterQuestObserver : '%s' holds stale tag '%s' — skipping subscribe. ")
				TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
				*GetOwner()->GetActorNameOrLabel(), *QuestTag.ToString());
			continue;
		}

		// Source annotation helps debugging the implicit-observed bridge — adopters who see a tag firing
		// delegates without an explicit ObservedTags entry can confirm it came from a derived component's
		// GetImplicitlyObservedTags() override.
		const bool bFromImplicitBridge = !ObservedTags.Contains(QuestTag);
		UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("UQuestObserverComponent::RegisterQuestObserver : Registered observer for tag: %s (%s)"),
			*QuestTag.ToString(),
			bFromImplicitBridge ? TEXT("implicit") : TEXT("authored"));
		
		// Live subscriptions — one per opted-in event type. Blocked/Unblocked subscribe directly to their own
		// dedicated events (no piggybacking on FQuestDeactivatedEvent).
		if (Settings.bObserveActivated)			SignalSubsystem->SubscribeMessage<FQuestActivatedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestActivated);
		if (Settings.bObserveActivationFailed)	SignalSubsystem->SubscribeMessage<FQuestActivationFailedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestActivationFailed);
		if (Settings.bObserveEnabled)			SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestEnabled);
		if (Settings.bObserveDisabled)			SignalSubsystem->SubscribeMessage<FQuestDisabledEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestDisabled);
		if (Settings.bObserveGiveBlocked)		SignalSubsystem->SubscribeMessage<FQuestGiveBlockedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestGiveBlocked);
		if (Settings.bObserveStarted)			SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestStarted);
		if (Settings.bObserveProgress)			SignalSubsystem->SubscribeMessage<FQuestProgressEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestProgress);
		if (Settings.bObserveCompleted)			SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestCompleted);
		if (Settings.bObserveDeactivated)		SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestDeactivated);
		if (Settings.bObserveBlocked)			SignalSubsystem->SubscribeMessage<FQuestBlockedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestBlocked);
		if (Settings.bObserveUnblocked)			SignalSubsystem->SubscribeMessage<FQuestUnblockedEvent>(QuestTag, this, &UQuestObserverComponent::HandleQuestUnblocked);

		if (!WorldState) continue;

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
		// No per-tag deduplication against live events here (unlike UQuestEventSubscription): the observer's catch-up runs
		// synchronously inside RegisterQuestObserver (called from BeginPlay), so there's no deferral window during
		// which a live event could fire and need deduplication. The K2 node defers to next tick to avoid racing the BP
		// execution stack; the observer has no such constraint.
		const TArray<FGameplayTag> CatchUpTags = FQuestCatchUpFanout::EnumerateTagsForCatchUp(QuestTag, StateSubsystem);

		for (const FGameplayTag& EachTag : CatchUpTags)
		{
			// EachTag is the canonical for this catch-up entry (post-GetQuestTagsUnderPrefix's alias-resolution).
			// Build the channel set [canonical, ...aliases] and pick the best match for this watched-key tag —
			// same selection the live bus dispatcher uses, so observer delegates see consistent MatchedChannel
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

			FQuestEventPayload Payload;
			Payload.NodeInfo.QuestTag = EachTag;

			// PendingGiver fact gates both Activated and Enabled catch-up.
			const FGameplayTag PendingFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::PendingGiver);
			const bool bIsPendingGiver = PendingFact.IsValid() && WorldState->HasFact(PendingFact);

			FQuestPrereqStatus CachedPrereqStatus;
			if (bIsPendingGiver && StateSubsystem)
			{
				CachedPrereqStatus = StateSubsystem->GetQuestPrereqStatus(EachTag);
			}

			if (Settings.bObserveActivated && bIsPendingGiver)
			{
				ActiveQuestTags.AddTag(EachTag);
				if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(EachTag, MatchedChannel, Payload, CachedPrereqStatus);
			}

			if (Settings.bObserveEnabled && bIsPendingGiver && CachedPrereqStatus.bSatisfied)
			{
				ActiveQuestTags.AddTag(EachTag);
				if (OnQuestEnabled.IsBound()) OnQuestEnabled.Broadcast(EachTag, MatchedChannel, Payload);
			}

			if (Settings.bObserveStarted)
			{
				const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Live);
				if (LiveFact.IsValid() && WorldState->HasFact(LiveFact))
				{
					ActiveQuestTags.AddTag(EachTag);
					AActor* RecoveredGiver = StateSubsystem ? StateSubsystem->GetLastGiverActor(EachTag) : nullptr;
					if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(EachTag, MatchedChannel, Payload, RecoveredGiver);
				}
			}

			// Disabled / GiveBlocked / Progress / Unblocked have no catch-up — transient or one-shot events without
			// recoverable state.

			if (Settings.bObserveCompleted)
			{
				const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Completed);
				if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact))
				{
					ActiveQuestTags.RemoveTag(EachTag);
					CompletedQuestTags.AddTag(EachTag);

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
					}
				}
			}

			if (Settings.bObserveDeactivated)
			{
				const FGameplayTag DeactivatedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Deactivated);
				if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
				{
					ActiveQuestTags.RemoveTag(EachTag);
					if (OnQuestDeactivated.IsBound()) OnQuestDeactivated.Broadcast(EachTag, MatchedChannel, Payload);
				}
			}

			if (Settings.bObserveBlocked)
			{
				const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(EachTag, EQuestStateLeaf::Blocked);
				if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
				{
					if (OnQuestBlocked.IsBound()) OnQuestBlocked.Broadcast(EachTag, MatchedChannel, Payload);
				}
			}
		}
	}
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