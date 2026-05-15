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
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "Signals/SignalSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "Utilities/QuestTagComposer.h"
#include "WorldState/WorldStateSubsystem.h"


UQuestGiverComponent::UQuestGiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestGiverComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterQuestGiver();
}

void UQuestGiverComponent::RegisterQuestGiver()
{
	if (QuestTagsToGive.IsEmpty())
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestGiverComponent::RegisterQuestGiver : QuestTagsToGive is empty. Actor: %s"),
			*GetOwner()->GetActorNameOrLabel());
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UQuestGiverComponent_RegisterQuestGiver);

	UWorldStateSubsystem* WorldState = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UWorldStateSubsystem>() : nullptr;

	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	for (const FGameplayTag& QuestTag : QuestTagsToGive)
	{
		if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
		{
			UE_LOG(LogSimpleQuest, Warning,
				TEXT("UQuestGiverComponent::RegisterQuestGiver : '%s' holds stale tag '%s' — skipping subscribe. ")
				TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
				*GetOwner()->GetActorNameOrLabel(), *QuestTag.ToString());
			continue;
		}

		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::RegisterQuestGiver : Registered giver for tag: %s on actor: %s"),
			*QuestTag.ToString(), *GetOwner()->GetName());

		if (SignalSubsystem)
		{
			SignalSubsystem->SubscribeMessage<FQuestActivatedEvent>  (QuestTag, this, &UQuestGiverComponent::OnQuestActivatedEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>    (QuestTag, this, &UQuestGiverComponent::OnQuestEnabledEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestDisabledEvent>   (QuestTag, this, &UQuestGiverComponent::OnQuestDisabledEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestStartedEvent>    (QuestTag, this, &UQuestGiverComponent::OnQuestStartedEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestDeactivatedEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestEndedEvent>      (QuestTag, this, &UQuestGiverComponent::OnQuestEndedEventReceived);
			SignalSubsystem->PublishMessage(Tag_Channel_QuestGiverRegistered, FQuestGiverRegisteredEvent(QuestTag));
		}

		// Catch-up: quest may have already reached PendingGiver state before this component came online.
		// Reconstruct Activated state, and if prereqs currently satisfy, Enabled state. Late-registering
		// givers end up in the same state as if they'd been listening when the events fired.
		if (IsValid(WorldState))
		{
			const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(
				FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::PendingGiver), false);
			if (PendingFact.IsValid() && WorldState->HasFact(PendingFact))
			{
				UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::RegisterQuestGiver : Catch-up — quest already pending giver: %s"),
					*QuestTag.ToString());

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
		}
	}

	// Coalesce the per-tag catch-up into a single availability-changed broadcast so UI refreshes
	// once rather than per tag during the subscription enumeration.
	if (ActivatedQuestTags.Num() > PriorActivated.Num() || EnabledQuestTags.Num() > PriorEnabled.Num())
	{
		BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::InitialCatchUp);
	}
}

void UQuestGiverComponent::OnQuestActivatedEventReceived(FGameplayTag Channel, const FQuestActivatedEvent& Event)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestActivatedEventReceived : '%s' (prereqs satisfied=%d)"),
		*Channel.ToString(), Event.PrereqStatus.bSatisfied ? 1 : 0);

	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	ActivatedQuestTags.AddTag(Channel);

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Activated);
}

void UQuestGiverComponent::OnQuestEnabledEventReceived(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestEnabledEventReceived : '%s' is now accept-ready"), *Channel.ToString());

	const FGameplayTagContainer PriorActivated = ActivatedQuestTags;
	const FGameplayTagContainer PriorEnabled = EnabledQuestTags;

	// Track on bound Channel (the tag this giver subscribed for via QuestTagsToGive), not Event.GetQuestTag().
	// Under multi-tag publish Event.QuestTag is the per-perspective canonical and varies across deliveries;
	// Channel stays stable as the giver's authored identity for bookkeeping.
	EnabledQuestTags.AddTag(Channel);

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Enabled);
}

void UQuestGiverComponent::OnQuestDisabledEventReceived(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestDisabledEventReceived : '%s' no longer accept-ready"), *Channel.ToString());

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

	// PendingGiveBlockedHandles holds an entry only between this giver's GiveQuest call and its outcome,
	// so its presence distinguishes "this giver gave the quest" from "some other path started it." Either
	// FQuestStartedEvent or FQuestGiveBlockedEvent closes the cycle. GivenQuestTags tracks history of
	// gives this specific giver issued — used by GetGivenQuests().
	if (PendingGiveBlockedHandles.Contains(Channel))
	{
		GivenQuestTags.AddTag(Channel);
	}
	UnsubscribePendingGiveBlocked(Channel);

	BroadcastAvailabilityChange(PriorActivated, PriorEnabled, EGiveAvailabilityChangeReason::Started);
}

void UQuestGiverComponent::OnQuestDeactivatedEventReceived(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
	HandleQuestLeftGiverSurface(Channel);
}

void UQuestGiverComponent::OnQuestEndedEventReceived(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
	// Only act when the quest is still in this giver's tracked state — i.e., it left without going
	// through the usual Started path (force-resolved while PendingGiver, save-rehydration of an
	// already-resolved quest). Started quests already cleared their state on
	// OnQuestStartedEventReceived; the RemoveTag calls in HandleQuestLeftGiverSurface would be
	// no-ops for those, but the early-return saves a delegate broadcast.
	if (!ActivatedQuestTags.HasTag(Channel) && !EnabledQuestTags.HasTag(Channel)) return;

	HandleQuestLeftGiverSurface(Channel);
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

	UE_LOG(LogSimpleQuest, Log, TEXT("UQuestGiverComponent::OnQuestGiveBlockedEventReceived : '%s' refused — %d blocker(s)"),
		*Channel.ToString(), Event.Blockers.Num());

	// Clear the one-shot subscription. Cycle closes on either this event or FQuestStartedEvent.
	UnsubscribePendingGiveBlocked(Channel);
}

void UQuestGiverComponent::GiveQuest(const FGameplayTag& QuestTag, const FQuestObjectiveActivationContext& Context)
{
	if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
	{
		UE_LOG(LogSimpleQuest, Warning,
			TEXT("UQuestGiverComponent::GiveQuest : '%s' on '%s' tried to give stale tag '%s' — skipping publish. ")
			TEXT("Use Stale Quest Tags (Tools → Debug → Stale Tags) to sweep this reference."),
			*GetClass()->GetName(), *GetOwner()->GetActorNameOrLabel(), *QuestTag.ToString());
		return;
	}

	if (!SignalSubsystem) return;

	// Subscribe one-shot to FQuestGiveBlockedEvent BEFORE publishing the give. The first response —
	// Blocked or Started — closes the cycle and clears the subscription. Replace any prior pending
	// subscription on this quest tag (most-recent attempt wins). PendingGiveBlockedHandles' presence
	// also marks "this giver has an in-flight give attempt for this tag" — read by
	// OnQuestStartedEventReceived to decide whether to fire OnQuestGiven.
	UnsubscribePendingGiveBlocked(QuestTag);
	FDelegateHandle BlockedHandle = SignalSubsystem->SubscribeMessage<FQuestGiveBlockedEvent>(
		QuestTag, this, &UQuestGiverComponent::OnQuestGiveBlockedEventReceived);
	PendingGiveBlockedHandles.Add(QuestTag, BlockedHandle);

	// Default the Instigator to this giver's owner if the caller didn't set one. Objectives commonly
	// need a "who activated me" reference; cheap default saves designers from remembering to set it.
	FQuestObjectiveActivationContext OutgoingContext = Context;
	if (!OutgoingContext.Dynamic.Instigator.IsValid())
	{
		OutgoingContext.Dynamic.Instigator = GetOwner();
	}
	// Seed OriginChain from OriginTag if a tag was supplied but the chain is empty.
	if (OutgoingContext.Dynamic.OriginTag.IsValid() && OutgoingContext.Dynamic.OriginChain.Num() == 0)
	{
		OutgoingContext.Dynamic.OriginChain.Add(OutgoingContext.Dynamic.OriginTag);
	}

	SignalSubsystem->PublishMessage(Tag_Channel_QuestGiven, FQuestGivenEvent(QuestTag, OutgoingContext));
}

void UQuestGiverComponent::GiveAllQuests(const FQuestObjectiveActivationContext& Context)
{
	if (EnabledQuestTags.IsEmpty())
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::GiveAllQuests : '%s' has no enabled quests; no-op."),
			*GetOwner()->GetActorNameOrLabel());
		return;
	}

	// Iterate QuestTagsToGive in authored order; give those that are currently enabled. Authoring
	// order gives designers control over the order of give calls (alphabetical / numeric /
	// by-importance — whatever they author).
	for (const FGameplayTag& QuestTag : QuestTagsToGive)
	{
		if (EnabledQuestTags.HasTag(QuestTag))
		{
			GiveQuest(QuestTag, Context);
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
	// the inverse — tags present before and absent now.
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

	// Skip broadcasts where no actual delta occurred — avoids noise on no-op state changes (e.g., a
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

int32 UQuestGiverComponent::ApplyTagRenames(const TMap<FName, FName>& Renames)
{
	int32 Count = 0;
	for (const auto& [OldName, NewName] : Renames)
	{
		FGameplayTag FoundOld;
		for (const FGameplayTag& Tag : QuestTagsToGive.GetGameplayTagArray())
		{
			if (Tag.GetTagName() == OldName)
			{
				FoundOld = Tag;
				break;
			}
		}
		if (FoundOld.IsValid())
		{
			QuestTagsToGive.RemoveTag(FoundOld);
			FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(NewName, false);
			if (NewTag.IsValid())
			{
				QuestTagsToGive.AddTag(NewTag);
			}
			Count++;
		}
	}
	return Count;
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