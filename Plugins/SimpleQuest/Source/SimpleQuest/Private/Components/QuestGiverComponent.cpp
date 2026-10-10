// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#include "Components/QuestGiverComponent.h"

#include "SimpleQuestLog.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestDisabledEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestGiverRegisteredEvent.h"
#include "Events/QuestStartedEvent.h"
#include "GameplayTagsManager.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "Subsystems/SignalSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "Utilities/QuestTagComposer.h"
#include "Subsystems/WorldStateSubsystem.h"


UQuestGiverComponent::UQuestGiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
}

void UQuestGiverComponent::PerformDeferredRegistration()
{
	Super::PerformDeferredRegistration();
	RegisterQuestGiver();
}

void UQuestGiverComponent::InitializeComponent()
{
	Super::InitializeComponent();
	DeclareGiverQuests();
}

void UQuestGiverComponent::DeclareGiverQuests()
{
	// Declaration only - no owner/world state read, so it's safe before BeginPlay. For placed actors this runs during
	// level load, ahead of every BeginPlay (where consumers fire questline-start), so the gate decision sees the giver.
	// The deferred catch-up (GiverCatchUpForQuest) still waits a tick for the owner to finish initializing.
	for (const FGameplayTag& QuestTag : QuestTagsToGive)
	{
		PublishGiverRegistration(QuestTag);
	}
}

void UQuestGiverComponent::PublishGiverRegistration(FGameplayTag QuestTag)
{
	if (!QuestTag.IsValid()) return;
	USignalSubsystem* Signals = SignalSubsystem;   // cached at BeginPlay; null on the early InitializeComponent path
	if (!Signals)
	{
		const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		Signals = GI ? GI->GetSubsystem<USignalSubsystem>() : nullptr;
	}
	if (Signals)
	{
		Signals->PublishMessage(Tag_Channel_QuestGiverRegistered, FQuestGiverRegisteredEvent(QuestTag));
	}
}

void UQuestGiverComponent::RegisterQuestGiver()
{
	if (QuestTagsToGive.IsEmpty())
	{
		UE_LOG(LogSimpleQuestSubscription, Warning, TEXT("UQuestGiverComponent::RegisterQuestGiver : QuestTagsToGive is empty. Actor: %s"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown"));
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UQuestGiverComponent_RegisterQuestGiver);

	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	for (const FGameplayTag& QuestTag : QuestTagsToGive)
	{
		SubscribeGiverQuest(QuestTag);
		GiverCatchUpForQuest(QuestTag);
	}

	if (ActivatedQuestTags.Num() > PriorActivated.Num() || EnabledQuestTags.Num() > PriorEnabled.Num())
	{
		BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::InitialCatchUp);
	}
}

void UQuestGiverComponent::SubscribeGiverQuest(FGameplayTag QuestTag)
{
	if (!SignalSubsystem || !QuestTag.IsValid()) return;

	if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
	{
		UE_LOG(LogSimpleQuestSubscription, Warning,
			TEXT("UQuestGiverComponent::SubscribeGiverQuest : '%s' holds stale tag '%s' - skipping subscribe. ")
			TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown"), *QuestTag.ToString());
		return;
	}

	// ExactOnly throughout: a Giver for "Quest X" cares about Quest X's own lifecycle, not the lifecycle of the
	// Steps inside it. Hierarchical delivery would force every handler to filter; a narrow subscription answers
	// the question at the bus instead.
	TArray<FDelegateHandle>& Handles = SubscriptionHandlesByTag.FindOrAdd(QuestTag);
	Handles.Add(SignalSubsystem->SubscribeMessage<FQuestActivatedEvent>  (QuestTag, this, &UQuestGiverComponent::OnQuestActivatedEventReceived, FSignalRoutingDefaults::ExactOnly));
	Handles.Add(SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>    (QuestTag, this, &UQuestGiverComponent::OnQuestEnabledEventReceived, FSignalRoutingDefaults::ExactOnly));
	Handles.Add(SignalSubsystem->SubscribeMessage<FQuestDisabledEvent>   (QuestTag, this, &UQuestGiverComponent::OnQuestDisabledEventReceived, FSignalRoutingDefaults::ExactOnly));
	Handles.Add(SignalSubsystem->SubscribeMessage<FQuestStartedEvent>    (QuestTag, this, &UQuestGiverComponent::OnQuestStartedEventReceived, FSignalRoutingDefaults::ExactOnly));
	Handles.Add(SignalSubsystem->SubscribeMessage<FQuestEndedEvent>      (QuestTag, this, &UQuestGiverComponent::OnQuestEndedEventReceived, FSignalRoutingDefaults::ExactOnly));
	Handles.Add(SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestDeactivatedEventReceived, FSignalRoutingDefaults::ExactOnly));

	PublishGiverRegistration(QuestTag);

	if (UQuestStateSubsystem* StateSubsystem = ResolveQuestStateSubsystem())
	{
		StateSubsystem->RegisterGiverSource(this, FGameplayTagContainer(QuestTag));  // per-tag, additive
	}
}

void UQuestGiverComponent::GiverCatchUpForQuest(FGameplayTag QuestTag)
{
	UWorldStateSubsystem* WorldState = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	if (!IsValid(WorldState)) return;

	const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::PendingGiver), false);
	if (!PendingFact.IsValid() || !WorldState->HasFact(PendingFact)) return;

	ActivatedQuestTags.AddTag(QuestTag);
	FQuestPrereqStatus PrereqStatus;
	if (UQuestStateSubsystem* StateSubsystem = ResolveQuestStateSubsystem())
	{
		PrereqStatus = StateSubsystem->GetQuestPrereqStatus(QuestTag);
	}
	if (PrereqStatus.bSatisfied)
	{
		EnabledQuestTags.AddTag(QuestTag);
	}
}

void UQuestGiverComponent::OnQuestActivatedEventReceived(FGameplayTag Channel, const FQuestActivatedEvent& Event)
{
	UE_LOG(LogSimpleQuestSubscription, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestActivatedEventReceived : '%s' (prereqs satisfied=%d)"),
		*Channel.ToString(), Event.PrereqStatus.bSatisfied ? 1 : 0);

	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	ActivatedQuestTags.AddTag(Channel);

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Activated);
}

void UQuestGiverComponent::OnQuestEnabledEventReceived(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
	UE_LOG(LogSimpleQuestSubscription, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestEnabledEventReceived : '%s' is now accept-ready"), *Channel.ToString());

	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	// Track on the bound Channel - the tag this giver subscribed for - rather than Event.GetQuestTag(). Under a
	// multi-tag publish the canonical identity varies across deliveries; Channel stays the giver's authored one.
	EnabledQuestTags.AddTag(Channel);

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Enabled);
}

void UQuestGiverComponent::OnQuestDisabledEventReceived(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
	UE_LOG(LogSimpleQuestSubscription, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestDisabledEventReceived : '%s' no longer accept-ready"), *Channel.ToString());

	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	EnabledQuestTags.RemoveTag(Channel);

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Disabled);
}

void UQuestGiverComponent::OnQuestStartedEventReceived(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	ActivatedQuestTags.RemoveTag(Channel);
	EnabledQuestTags.RemoveTag(Channel);

	// PendingGiveBlockedHandles holds an entry only between this giver's GiveQuest call and its outcome, so its
	// presence is what distinguishes "this giver gave it" from "something else started the same quest." Either
	// this event or FQuestGiveBlockedEvent closes the cycle. GivenQuestTags is the history GetGivenQuests reads.
	const bool bThisGiverGaveIt = PendingGiveBlockedHandles.Contains(Channel);
	if (bThisGiverGaveIt)
	{
		GivenQuestTags.AddTag(Channel);
	}
	UnsubscribePendingGiveBlocked(Channel);

	// State is complete before anything broadcasts, so a handler reading GivenQuestTags / ActivatedQuestTags /
	// EnabledQuestTags sees the finished picture. That ordering is now a property of this function rather than of
	// which subscription the bus happened to register first.
	//
	// Gating on bThisGiverGaveIt is also what keeps a restore quiet: the flag is only ever set by a live GiveQuest
	// call, so a quest already running when this component registers can never fire a give that never happened.
	if (bThisGiverGaveIt && OnQuestGiven.IsBound())
	{
		OnQuestGiven.Broadcast(Channel, Event.Payload);
	}

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Started);
}

void UQuestGiverComponent::OnQuestDeactivatedEventReceived(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
	HandleQuestLeftGiverSurface(Channel);
}

void UQuestGiverComponent::OnQuestEndedEventReceived(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
	// Cleanup runs only while the quest is still in this Giver's tracked state - i.e. it left without going
	// through the usual Started path (force-resolved while PendingGiver, or a save rehydrating an already-
	// resolved quest). A started quest cleared its state in OnQuestStartedEventReceived; repeating it here
	// would be a no-op on state but would double-fire OnGiveAvailabilityChanged.
	if (!ActivatedQuestTags.HasTag(Channel) && !EnabledQuestTags.HasTag(Channel)) return;

	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	ActivatedQuestTags.RemoveTag(Channel);
	EnabledQuestTags.RemoveTag(Channel);
	UnsubscribePendingGiveBlocked(Channel);

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Deactivated);
}

void UQuestGiverComponent::HandleQuestLeftGiverSurface(FGameplayTag Channel)
{
	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	ActivatedQuestTags.RemoveTag(Channel);
	EnabledQuestTags.RemoveTag(Channel);

	UnsubscribePendingGiveBlocked(Channel);

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Deactivated);
}

void UQuestGiverComponent::OnQuestGiveBlockedEventReceived(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
	// Only handle blocker events that originated from this giver's give attempt. Multiple givers may
	// subscribe to the same quest tag channel; GiverActor identifies the initiator.
	if (Event.GiverActor.Get() != GetOwner()) return;

	UE_LOG(LogSimpleQuestSubscription, Log, TEXT("UQuestGiverComponent::OnQuestGiveBlockedEventReceived : '%s' refused - %d blocker(s)"),
		*Channel.ToString(), Event.Blockers.Num());

	if (OnGiveRefused.IsBound())
	{
		OnGiveRefused.Broadcast(Channel, Event.Blockers);
	}

	// Clear the one-shot subscription. Cycle closes on either this event or FQuestStartedEvent.
	UnsubscribePendingGiveBlocked(Channel);
}

void UQuestGiverComponent::GiveQuest(const FGameplayTag& QuestTag, const FQuestObjectiveActivationParams& Context)
{
	if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
	{
		UE_LOG(LogSimpleQuestSubscription, Warning,
			TEXT("UQuestGiverComponent::GiveQuest : '%s' on '%s' tried to give stale tag '%s' - skipping publish. ")
			TEXT("Use Stale Quest Tags (Tools → Debug → Stale Tags) to sweep this reference."),
			*GetClass()->GetName(), *GetOwner()->GetActorNameOrLabel(), *QuestTag.ToString());
		return;
	}

	if (!SignalSubsystem) return;

	// Subscribe one-shot to FQuestGiveBlockedEvent BEFORE publishing the give. The first response -
	// Blocked or Started - closes the cycle and clears the subscription. Replace any prior pending
	// subscription on this quest tag (most-recent attempt wins). PendingGiveBlockedHandles' presence
	// also marks "this giver has an in-flight give attempt for this tag" - read by
	// HandleQuestStarted to attribute the give to this giver (sets GiverActor = GetOwner() on the
	// inherited OnQuestStarted broadcast payload).
	UnsubscribePendingGiveBlocked(QuestTag);
	FDelegateHandle BlockedHandle = SignalSubsystem->SubscribeMessage<FQuestGiveBlockedEvent>(
		QuestTag, this, &UQuestGiverComponent::OnQuestGiveBlockedEventReceived);
	PendingGiveBlockedHandles.Add(QuestTag, BlockedHandle);

	// Default the Instigator to this giver's owner if the caller didn't set one. Objectives commonly
	// need a "who activated me" reference; cheap default saves designers from remembering to set it.
	FQuestObjectiveActivationParams OutgoingContext = Context;
	if (!OutgoingContext.Instigator.IsValid())
	{
		OutgoingContext.Instigator = GetOwner();
	}
	if (OutgoingContext.OriginTag.IsValid() && OutgoingContext.OriginChain.Num() == 0)
	{
		OutgoingContext.OriginChain.Add(OutgoingContext.OriginTag);
	}

	SignalSubsystem->PublishMessage(Tag_Channel_QuestGiven, FQuestGivenEvent(QuestTag, OutgoingContext));
}

void UQuestGiverComponent::GiveAllQuests(const FQuestObjectiveActivationParams& Context)
{
	if (EnabledQuestTags.IsEmpty())
	{
		UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("UQuestGiverComponent::GiveAllQuests : '%s' has no enabled quests; no-op."),
			*GetOwner()->GetActorNameOrLabel());
		return;
	}

	// Iterate QuestTagsToGive in authored order; give those that are currently enabled. Authoring
	// order gives designers control over the order of give calls (alphabetical / numeric /
	// by-importance - whatever they author).
	for (const FGameplayTag& QuestTag : QuestTagsToGive)
	{
		if (EnabledQuestTags.HasTag(QuestTag))
		{
			GiveQuest(QuestTag, Context);
		}
	}
}

void UQuestGiverComponent::AddTagsToGive(const FGameplayTagContainer& Tags)
{
	for (const FGameplayTag& Tag : Tags)
	{
		if (!Tag.IsValid() || QuestTagsToGive.HasTagExact(Tag)) continue;
		QuestTagsToGive.AddTag(Tag);

		if (bRegistered)
		{
			const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
			const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

			// Giver side only. A quest this component offers is not an observed tag, so adding one at runtime
			// installs the giver's own subscriptions and leaves the Observer delegates reporting exactly what
			// ObservedTags lists - the same contract an authored tag gets.
			SubscribeGiverQuest(Tag);
			GiverCatchUpForQuest(Tag);

			if (ActivatedQuestTags.Num() > PriorActivated.Num() || EnabledQuestTags.Num() > PriorEnabled.Num())
			{
				BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::InitialCatchUp);
			}
		}
	}
}

void UQuestGiverComponent::RemoveTagsFromGive(const FGameplayTagContainer& Tags)
{
	for (const FGameplayTag& Tag : Tags)
	{
		if (!QuestTagsToGive.HasTagExact(Tag)) continue;
		QuestTagsToGive.RemoveTag(Tag);

		if (bRegistered)
		{
			// If the giver is currently offering this quest, fire the availability change (NewlyUnavailable /
			// NewlyDeactivated) so UI tears down - un-listing mid-availability must balance the pair.
			// HandleQuestLeftGiverSurface does the state removal + broadcast.
			if (ActivatedQuestTags.HasTagExact(Tag) || EnabledQuestTags.HasTagExact(Tag))
			{
				HandleQuestLeftGiverSurface(Tag);
			}

			UnregisterSingleObservedTag(Tag);

			if (UQuestStateSubsystem* StateSubsystem = ResolveQuestStateSubsystem())
			{
				StateSubsystem->UnregisterGiverSource(this, Tag);
			}
		}
	}
}

void UQuestGiverComponent::BroadcastAvailabilityChange(const FGameplayTagContainer& PriorActivated, const FGameplayTagContainer& PriorEnabled, EGiveAvailabilityChangeReason Reason)
{
	if (!OnGiveAvailabilityChanged.IsBound()) return;

	FGiveAvailabilityChange Change;
	Change.Reason = Reason;
	Change.CurrentActivated = ActivatedQuestTags;
	Change.CurrentEnabled = EnabledQuestTags;

	// Compute deltas. "Newly entered" sets are tags present now and absent before; "newly left" sets are
	// the inverse - tags present before and absent now.
	for (const FGameplayTag& Tag : ActivatedQuestTags)
	{
		if (!PriorActivated.HasTagExact(Tag)) Change.NewlyActivated.AddTag(Tag);
	}
	for (const FGameplayTag& Tag : PriorActivated)
	{
		if (!ActivatedQuestTags.HasTagExact(Tag)) Change.NewlyDeactivated.AddTag(Tag);
	}
	for (const FGameplayTag& Tag : EnabledQuestTags)
	{
		if (!PriorEnabled.HasTagExact(Tag)) Change.NewlyEnabled.AddTag(Tag);
	}
	for (const FGameplayTag& Tag : PriorEnabled)
	{
		if (!EnabledQuestTags.HasTagExact(Tag)) Change.NewlyUnavailable.AddTag(Tag);
	}

	// Skip broadcasts where no actual delta occurred - avoids noise on no-op state changes (e.g., a
	// duplicate FQuestEnabledEvent for an already-enabled quest under a multi-publish path).
	if (Change.NewlyActivated.IsEmpty() && Change.NewlyDeactivated.IsEmpty()
	 && Change.NewlyEnabled.IsEmpty() && Change.NewlyUnavailable.IsEmpty())
	{
		return;
	}

	OnGiveAvailabilityChanged.Broadcast(Change);
}

void UQuestGiverComponent::UnsubscribePendingGiveBlocked(FGameplayTag QuestTag)
{
	if (FDelegateHandle* Handle = PendingGiveBlockedHandles.Find(QuestTag))
	{
		if (SignalSubsystem) SignalSubsystem->UnsubscribeMessage(QuestTag, *Handle);
		PendingGiveBlockedHandles.Remove(QuestTag);
	}
}

TArray<FQuestActivationBlocker> UQuestGiverComponent::QueryActivationBlockers(FGameplayTag QuestTag) const
{
	if (UQuestStateSubsystem* StateSubsystem = ResolveQuestStateSubsystem())
	{
		return StateSubsystem->QueryQuestActivationBlockers(QuestTag);
	}
	return TArray<FQuestActivationBlocker>();
}

UQuestStateSubsystem* UQuestGiverComponent::ResolveQuestStateSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UQuestStateSubsystem>();
		}
	}
	return nullptr;
}

FGameplayTagContainer UQuestGiverComponent::GetRegisteredQuestTagsToGive() const
{
	return FQuestTagComposer::FilterToRegisteredTags(
		QuestTagsToGive,
		FString::Printf(TEXT("UQuestGiverComponent::GetRegisteredQuestTagsToGive ('%s')"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

int32 UQuestGiverComponent::RemoveTags(const TArray<FGameplayTag>& TagsToRemove)
{
	int32 Count = 0;
	for (const FGameplayTag& Tag : TagsToRemove)
	{
		if (QuestTagsToGive.HasTagExact(Tag))
		{
			if (Count == 0) Modify();
			QuestTagsToGive.RemoveTag(Tag);
			++Count;
		}
	}
	if (Count > 0 && GetOwner())
	{
		GetOwner()->MarkPackageDirty();
	}
	return Count;
}

void UQuestGiverComponent::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	if (!QuestTagsToGive.IsEmpty())
	{
		TArray<FString> TagStrings;
		TagStrings.Reserve(QuestTagsToGive.Num());
		for (const FGameplayTag& Tag : QuestTagsToGive)
		{
			TagStrings.Add(Tag.ToString());
		}
		Context.AddTag(FAssetRegistryTag(TEXT("QuestTagsToGive"), FString::Join(TagStrings, TEXT("|")), FAssetRegistryTag::TT_Hidden));
	}
}
