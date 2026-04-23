// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestGiverComponent.h"

#include "SimpleQuestLog.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestGiverRegisteredEvent.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
#include "GameplayTagsManager.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
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

void UQuestGiverComponent::RegisterQuestGiver()
{
	if (QuestTagsToGive.IsEmpty())
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestGiverComponent::RegisterQuestGiver : QuestTagsToGive is empty. Actor: %s"),	*GetOwner()->GetActorNameOrLabel());
		return;
	}

	UWorldStateSubsystem* WorldState = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UWorldStateSubsystem>() : nullptr;

	for (const FGameplayTag& QuestTag : QuestTagsToGive)
	{
		if (!QuestTag.IsValid()) continue;

		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::RegisterQuestGiver : Registered giver for tag: %s on actor: %s"), *QuestTag.ToString(), *GetOwner()->GetName());

		if (SignalSubsystem)
		{
			SignalSubsystem->SubscribeMessage<FQuestEnabledEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestEnabledEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestStartedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestStartedEventReceived);
			SignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(QuestTag, this, &UQuestGiverComponent::OnQuestDeactivatedEventReceived);
			SignalSubsystem->PublishMessage(Tag_Channel_QuestGiverRegistered, FQuestGiverRegisteredEvent(QuestTag));
		}
		// Catch-up: quest may have become giver-gated before this component came online
		if (WorldState)
		{
			const FGameplayTag PendingFact = UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver), false);
			if (WorldState->HasFact(PendingFact))
			{
				UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::RegisterQuestGiver : Catch-up — quest already pending giver: %s"), *QuestTag.ToString());
				SetQuestGiverActivated(QuestTag, true);
			}
		}
	}
}

void UQuestGiverComponent::OnQuestEnabledEventReceived(FGameplayTag Channel, const FQuestEnabledEvent& Event)
{
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("UQuestGiverComponent::OnQuestEnabledEventReceived : Event tag: %s : Event type: %s : Owner: %s"), *Channel.ToString(), *Event.StaticStruct()->GetFName().ToString(), *GetOwner()->GetClass()->GetFName().ToString());

	SetQuestGiverActivated(Event.GetQuestTag(), true);
}

void UQuestGiverComponent::OnQuestStartedEventReceived(FGameplayTag Channel, const FQuestStartedEvent& Event)
{
	SetQuestGiverActivated(Event.GetQuestTag(), false);
}

void UQuestGiverComponent::OnQuestDeactivatedEventReceived(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
	SetQuestGiverActivated(Event.GetQuestTag(), false);
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
	if (QuestTag.IsValid() && SignalSubsystem)
	{
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

void UQuestGiverComponent::SetQuestGiverActivated(const FGameplayTag& QuestTag, bool bIsQuestActive)
{
	if (bIsQuestActive)
	{
		EnabledQuestTags.AddTag(QuestTag);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::SetQuestGiverActivated : Quest enabled: %s"), *QuestTag.ToString());
	}
	else
	{
		EnabledQuestTags.RemoveTag(QuestTag);
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestGiverComponent::SetQuestGiverActivated : Quest disabled: %s"), *QuestTag.ToString());
	}
	OnQuestGiverActivated.Broadcast(bIsQuestActive, QuestTag, CanGiveAnyQuests());
}

bool UQuestGiverComponent::CanGiveAnyQuests() const
{
	return !EnabledQuestTags.IsEmpty();
}

bool UQuestGiverComponent::IsQuestEnabled(FGameplayTag QuestTag)
{
	return EnabledQuestTags.HasTag(QuestTag);
}

