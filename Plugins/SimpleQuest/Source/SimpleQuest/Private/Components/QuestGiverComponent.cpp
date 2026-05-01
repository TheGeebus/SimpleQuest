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
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
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
			SignalSubsystem->PublishMessage(Tag_Channel_QuestGiverRegistered, FQuestGiverRegisteredEvent(QuestTag));
		}

		// Catch-up: quest may have already reached PendingGiver state before this component came online.
		// Reconstruct Activated state, and if prereqs currently satisfy, Enabled state. Late-registering
		// observers end up in the same state as if they'd been listening when the events fired.
		if (IsValid(WorldState))
		{
			const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(
				FQuestTagComposer::MakeStateFact(QuestTag, FQuestTagComposer::Leaf_PendingGiver), false);
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

	const FGameplayTag QuestTag = Event.GetQuestTag();
	EnabledQuestTags.AddTag(QuestTag);
	if (OnQuestEnabled.IsBound()) OnQuestEnabled.Broadcast(QuestTag);
}

void UQuestGiverComponent::OnQuestStartedEventReceived(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
	const FGameplayTag QuestTag = Event.GetQuestTag();
	ActivatedQuestTags.RemoveTag(QuestTag);
	EnabledQuestTags.RemoveTag(QuestTag);

	// FQuestStartedEvent is the symmetric "Yes" partner to FQuestGiveBlockedEvent — clear any pending blocker
	// subscription for this quest tag. Either response (Blocked or Started) closes the give-attempt cycle.
	UnsubscribePendingGiveBlocked(QuestTag);

	if (OnQuestStarted.IsBound()) OnQuestStarted.Broadcast(QuestTag);
}

void UQuestGiverComponent::OnQuestDeactivatedEventReceived(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
	const FGameplayTag QuestTag = Event.GetQuestTag();
	ActivatedQuestTags.RemoveTag(QuestTag);
	EnabledQuestTags.RemoveTag(QuestTag);

	UnsubscribePendingGiveBlocked(QuestTag);  // quest interruption ends the attempt cycle

	if (OnQuestDeactivated.IsBound()) OnQuestDeactivated.Broadcast(QuestTag);
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

void UQuestGiverComponent::GiveQuestByTag(const FGameplayTag& QuestTag, const FQuestObjectiveActivationParams& Params)
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
		FQuestObjectiveActivationParams OutgoingParams = ActivationParams;
		OutgoingParams.TargetActors.Append(Params.TargetActors);
		OutgoingParams.TargetClasses.Append(Params.TargetClasses);
		OutgoingParams.NumElementsRequired += Params.NumElementsRequired;

		// Single-valued fields: caller wins if set, else keep the authored baseline.
		if (Params.ActivationSource) OutgoingParams.ActivationSource = Params.ActivationSource;
		if (Params.CustomData.IsValid()) OutgoingParams.CustomData = Params.CustomData;
		if (Params.OriginTag.IsValid()) OutgoingParams.OriginTag = Params.OriginTag;
		if (Params.OriginChain.Num() > 0) OutgoingParams.OriginChain = Params.OriginChain;

		// Default ActivationSource to the giver's owner when neither authored nor caller set it.
		if (!OutgoingParams.ActivationSource)
		{
			OutgoingParams.ActivationSource = GetOwner();
		}
		// Seed OriginChain from OriginTag if the designer authored a tag but the chain is still empty.
		if (OutgoingParams.OriginTag.IsValid() && OutgoingParams.OriginChain.Num() == 0)
		{
			OutgoingParams.OriginChain.Add(OutgoingParams.OriginTag);
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
	const FGameplayTag QuestTag = Event.GetQuestTag();
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestActivatedEventReceived : '%s' (prereqs satisfied=%d)"),
		*QuestTag.ToString(), Event.PrereqStatus.bSatisfied ? 1 : 0);

	ActivatedQuestTags.AddTag(QuestTag);
	if (OnQuestActivated.IsBound()) OnQuestActivated.Broadcast(QuestTag, Event.PrereqStatus);
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

