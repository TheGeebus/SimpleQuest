// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Components/QuestRewardRecipientComponent.h"

#include "Events/QuestRewardGrantedEvent.h"
#include "Subsystems/SignalSubsystem.h"
#include "GameFramework/Actor.h"
#include "SimpleQuestLog.h"

void UQuestRewardRecipientComponent::BeginPlay()
{
	Super::BeginPlay();   // UQuestComponentBase caches SignalSubsystem

	if (!SignalSubsystem) return;

	// Subscribe each reward-type channel. Hierarchical routing (the bus default) means a parent type catches its
	// descendants. No catch-up needed: grants are live-only, and a grant only fires when quest flow reaches a reward
	// node (mid-gameplay), so subscribing here — component BeginPlay, ahead of the owner's BP BeginPlay — misses nothing.
	for (const TPair<FGameplayTag, FRewardTypeSubscription>& Pair : ReactsToRewardTypes)
	{
		if (!Pair.Key.IsValid()) continue;
		SignalSubsystem->SubscribeMessage<FQuestRewardGrantedEvent>(Pair.Key, this, &UQuestRewardRecipientComponent::HandleRewardGranted, Pair.Value.Routing);
		UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestRewardRecipientComponent[%s]: subscribed to reward type '%s' (routing=%s)"),
			*GetNameSafe(GetOwner()),
			*Pair.Key.ToString(),
			*UEnum::GetValueAsString(Pair.Value.Routing));
	}
}

void UQuestRewardRecipientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SignalSubsystem)
	{
		SignalSubsystem->UnsubscribeListener(this);   // drops every subscription this component made
	}
	Super::EndPlay(EndPlayReason);
}

void UQuestRewardRecipientComponent::HandleRewardGranted(FGameplayTag Channel, const FQuestRewardGrantedEvent& Event)
{
	if (!DoesRewardTargetMe(Event.Grant)) return;

	UE_LOG(LogSimpleQuestActivation, Log, TEXT("UQuestRewardRecipientComponent[%s]: received grant '%s' — broadcasting OnRewardGranted"),
		*GetNameSafe(GetOwner()),
		*Event.Grant.RewardType.ToString());

	OnRewardGranted.Broadcast(Event.Grant);
}

bool UQuestRewardRecipientComponent::DoesRewardTargetMe_Implementation(const FQuestRewardContext& Grant) const
{
	// Broadcast (no explicit recipient) reaches everyone subscribed to the type.
	if (!Grant.Recipient.IsValid()) return true;

	const AActor* Owner  = GetOwner();
	const AActor* Target = Grant.Recipient.Get();
	// Targeted at my owner, or at an actor that owns my owner (e.g. the controller of my pawn).
	return Target == Owner || (Owner && Target == Owner->GetOwner());
}