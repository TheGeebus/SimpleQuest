// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestGiverComponent.h"

#include "SimpleQuestLog.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestGiverRegisteredEvent.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "GameplayTagsManager.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestDisabledEvent.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "UObject/AssetRegistryTagsContext.h"


UQuestGiverComponent::UQuestGiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestGiverComponent::BeginPlay()
{
	Super::BeginPlay();

	RegisterQuestGiver();
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
			// Lazy Modify — only snapshot on first actual removal. No-op when no transaction active.
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

void UQuestGiverComponent::RegisterQuestGiver()
{
	if (QuestTagsToGive.IsEmpty())
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestGiverComponent::RegisterQuestGiver : QuestTagsToGive is empty. Actor: %s"),	*GetOwner()->GetActorNameOrLabel());
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UQuestGiverComponent_RegisterQuestGiver);

	UWorldStateSubsystem* WorldState = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UWorldStateSubsystem>() : nullptr;

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

		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::RegisterQuestGiver : Registered giver for tag: %s on actor: %s"), *QuestTag.ToString(), *GetOwner()->GetName());

		if (SignalSubsystem)
		{
			SignalSubsystem->SubscribeMessage<FQuestActivatedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestActivatedEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestEnabledEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestDisabledEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestDisabledEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestStartedEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestDeactivatedEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestEndedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestEndedEventReceived);
			SignalSubsystem->PublishMessage(Tag_Channel_QuestGiverRegistered, FQuestGiverRegisteredEvent(QuestTag));
		}

		// Catch-up: quest may have already reached PendingGiver state before this component came online.
		// Reconstruct Activated state, and if prereqs currently satisfy, Enabled state. Late-registering
		// observers end up in the same state as if they'd been listening when the events fired.
		if (IsValid(WorldState))
		{
			const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(
				FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::PendingGiver), false);
			if (PendingFact.IsValid() && WorldState->HasFact(PendingFact))
			{
				UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::RegisterQuestGiver : Catch-up — quest already pending giver: %s"), *QuestTag.ToString());

				ActivatedQuestTags.AddTag(QuestTag);
				FQuestPrereqStatus PrereqStatus;
				if (UQuestStateSubsystem* StateSubsystem = ResolveQuestStateSubsystem())
				{
					PrereqStatus = StateSubsystem->GetQuestPrereqStatus(QuestTag);
				}
				if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(QuestTag, PrereqStatus);

				if (PrereqStatus.bSatisfied)
				{
					EnabledQuestTags.AddTag(QuestTag);
					if (OnQuestEnabled.IsBound()) OnQuestEnabled.Broadcast(QuestTag);
				}
			}
		}
	}
}

void UQuestGiverComponent::OnQuestEnabledEventReceived(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestEnabledEventReceived : '%s' is now accept-ready"), *Channel.ToString());

	// Track what we're subscribed on (Channel — the tag this giver registered for via QuestTagsToGive),
	// NOT the publisher's canonical (Event.GetQuestTag()). Under multi-tag publish, a single subscription
	// receives multiple deliveries with different Event.QuestTag values (one per perspective canonical of
	// the same logical quest); using Event.QuestTag would pollute the container with per-perspective
	// canonicals the designer never authored. Channel is the giver's authored identity for this quest;
	// GiveQuestByTag(Channel) alias-walks naturally to all placements.
	EnabledQuestTags.AddTag(Channel);
	if (OnQuestEnabled.IsBound()) OnQuestEnabled.Broadcast(Channel);
}

void UQuestGiverComponent::OnQuestStartedEventReceived(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
	ActivatedQuestTags.RemoveTag(Channel);
	EnabledQuestTags.RemoveTag(Channel);

	// FQuestStartedEvent is the symmetric "Yes" partner to FQuestGiveBlockedEvent — clear any pending blocker
	// subscription for this quest tag. Either response (Blocked or Started) closes the give-attempt cycle.
	UnsubscribePendingGiveBlocked(Channel);

	if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(Channel);
}

void UQuestGiverComponent::OnQuestDeactivatedEventReceived(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
	HandleQuestLeftGiverSurface(Channel);
}

void UQuestGiverComponent::OnQuestEndedEventReceived(FGameplayTag Channel, const FQuestEndedEvent& Event)
{
	// Only act when the quest is still in this giver's tracked state — i.e., it left without going through the
	// usual Started path (force-resolved while PendingGiver, save-rehydration of an already-resolved quest,
	// etc.). Completed-after-Started quests already cleared their state on OnQuestStartedEventReceived; the
	// RemoveTag calls below would be no-ops for those, but the early-return saves a delegate broadcast.
	if (!ActivatedQuestTags.HasTag(Channel) && !EnabledQuestTags.HasTag(Channel)) return;

	HandleQuestLeftGiverSurface(Channel);
}

void UQuestGiverComponent::HandleQuestLeftGiverSurface(FGameplayTag Channel)
{
	ActivatedQuestTags.RemoveTag(Channel);
	EnabledQuestTags.RemoveTag(Channel);

	UnsubscribePendingGiveBlocked(Channel);  // quest interruption ends the attempt cycle

	if (OnQuestDeactivated.IsBound()) OnQuestDeactivated.Broadcast(Channel);
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

void UQuestGiverComponent::GiveQuestByTag(const FGameplayTag& QuestTag, const FQuestObjectiveActivationContext& Params)
{
	if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
	{
		UE_LOG(LogSimpleQuest, Warning,
			TEXT("UQuestGiverComponent::GiveQuestByTag : '%s' on '%s' tried to give stale tag '%s' — skipping publish. ")
			TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to sweep this reference."),
			*GetClass()->GetName(), *GetOwner()->GetActorNameOrLabel(), *QuestTag.ToString());
		return;
	}
	if (SignalSubsystem)
	{
		// Subscribe one-shot to FQuestGiveBlockedEvent BEFORE publishing the give. The first response — Blocked or
		// Started — closes the cycle and clears the subscription. Replace any prior pending subscription on this
		// quest tag (most-recent attempt wins).
		UnsubscribePendingGiveBlocked(QuestTag);
		FDelegateHandle BlockedHandle = SignalSubsystem->SubscribeMessage<FQuestGiveBlockedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestGiveBlockedEventReceived);
		PendingGiveBlockedHandles.Add(QuestTag, BlockedHandle);

		// Start from the designer-authored baseline, then additively merge the caller-supplied Params on top. Matches
		// the step-side merge rule so composition semantics are uniform across the activation pipeline.
		FQuestObjectiveActivationContext OutgoingParams = ActivationParams;
		OutgoingParams.Dynamic.TargetActors.Append(Params.Dynamic.TargetActors);
		OutgoingParams.Authored.TargetClasses.Append(Params.Authored.TargetClasses);
		OutgoingParams.Authored.NumElementsRequired += Params.Authored.NumElementsRequired;

		// Single-valued fields: caller wins if set, else keep the authored baseline.
		if (Params.Dynamic.Instigator.IsValid()) OutgoingParams.Dynamic.Instigator = Params.Dynamic.Instigator;
		if (Params.Dynamic.CustomData.IsValid()) OutgoingParams.Dynamic.CustomData = Params.Dynamic.CustomData;
		if (Params.Dynamic.OriginTag.IsValid()) OutgoingParams.Dynamic.OriginTag = Params.Dynamic.OriginTag;
		if (Params.Dynamic.OriginChain.Num() > 0) OutgoingParams.Dynamic.OriginChain = Params.Dynamic.OriginChain;

		// Default Instigator to the giver's owner when neither authored nor caller set it.
		if (!OutgoingParams.Dynamic.Instigator.IsValid())
		{
			OutgoingParams.Dynamic.Instigator = GetOwner();
		}
		// Seed OriginChain from OriginTag if the designer authored a tag but the chain is still empty.
		if (OutgoingParams.Dynamic.OriginTag.IsValid() && OutgoingParams.Dynamic.OriginChain.Num() == 0)
		{
			OutgoingParams.Dynamic.OriginChain.Add(OutgoingParams.Dynamic.OriginTag);
		}

		SignalSubsystem->PublishMessage(Tag_Channel_QuestGiven, FQuestGivenEvent(QuestTag, OutgoingParams));
	}
}

bool UQuestGiverComponent::CanGiveAnyQuests() const
{
	return !EnabledQuestTags.IsEmpty();
}

FGameplayTagContainer UQuestGiverComponent::GetRegisteredQuestTagsToGive() const
{
	return FQuestTagComposer::FilterToRegisteredTags(
		QuestTagsToGive,
		FString::Printf(TEXT("UQuestGiverComponent::GetRegisteredQuestTagsToGive ('%s')"),
			GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("unknown")));
}

bool UQuestGiverComponent::IsQuestEnabled(FGameplayTag QuestTag)
{
	return EnabledQuestTags.HasTag(QuestTag);
}

void UQuestGiverComponent::OnQuestActivatedEventReceived(FGameplayTag Channel, const FQuestActivatedEvent& Event)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestActivatedEventReceived : '%s' (prereqs satisfied=%d)"),
		*Channel.ToString(),
		Event.PrereqStatus.bSatisfied ? 1 : 0);

	ActivatedQuestTags.AddTag(Channel);
	if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(Channel, Event.PrereqStatus);
}

void UQuestGiverComponent::OnQuestDisabledEventReceived(FGameplayTag Channel, const FQuestDisabledEvent& Event)
{
	const FGameplayTag QuestTag = Event.GetQuestTag();
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestDisabledEventReceived : '%s' no longer accept-ready"),
		*QuestTag.ToString());

	EnabledQuestTags.RemoveTag(QuestTag);
	if (OnQuestDisabled.IsBound()) OnQuestDisabled.Broadcast(QuestTag);
}

void UQuestGiverComponent::OnQuestGiveBlockedEventReceived(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
	// Filter to events that originated from this giver's give attempt — defensive against future scenarios
	// where multiple givers might somehow subscribe to the same blocker channel. Under the current per-attempt
	// subscription model the filter is belt-and-suspenders; the giver only subscribes during its own attempt.
	if (Event.GiverActor.Get() != GetOwner()) return;

	const FGameplayTag QuestTag = Event.GetQuestTag();
	UE_LOG(LogSimpleQuest, Log, TEXT("UQuestGiverComponent::OnQuestGiveBlockedEventReceived : '%s' refused — %d blocker(s)"),
		*QuestTag.ToString(), Event.Blockers.Num());

	if (OnQuestGiveBlocked.IsBound()) OnQuestGiveBlocked.Broadcast(QuestTag, Event.Blockers);

	// One-shot — clear our subscription. The give attempt's cycle is closed by either this event or
	// FQuestStartedEvent (handled in OnQuestStartedEventReceived).
	UnsubscribePendingGiveBlocked(QuestTag);
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

