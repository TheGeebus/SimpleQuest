// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestTagComposer.h"
#include "GameplayTagsManager.h"
#include "BlueprintAsync/QuestEventSubscription.h"
#include "Engine/GameInstance.h"
#include "Events/QuestActivationRequestEvent.h"
#include "Events/QuestBlockRequestEvent.h"
#include "Events/QuestClearBlockRequestEvent.h"
#include "Events/QuestDeactivateRequestEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestlineStartRequestEvent.h"
#include "Events/QuestResolveRequestEvent.h"
#include "Subsystems/QuestManagerSubsystem.h"


// -------------------------------------------------------------------------
// Private helpers
// -------------------------------------------------------------------------

UWorldStateSubsystem* USimpleQuestBlueprintLibrary::GetWorldState(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    const UWorld* World = WorldContext->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
}

USignalSubsystem* USimpleQuestBlueprintLibrary::GetSignalSubsystem(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    const UWorld* World = WorldContext->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<USignalSubsystem>() : nullptr;
}

UQuestManagerSubsystem* USimpleQuestBlueprintLibrary::GetQuestManager(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    const UWorld* World = WorldContext->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UQuestManagerSubsystem>() : nullptr;
}


// -------------------------------------------------------------------------
// Quest state queries
// -------------------------------------------------------------------------

bool USimpleQuestBlueprintLibrary::IsQuestLive(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsLive(GetWorldState(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestCompleted(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsCompleted(GetWorldState(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestPendingGiver(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsPendingGiver(GetWorldState(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestResolvedWith(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag)
{
    if (!QuestTag.IsValid() || !OutcomeTag.IsValid()) return false;
    // UQuestStateSubsystem::HasResolvedWith works for any OutcomeTag the quest has actually fired with,
    // including dynamic outcomes set via the BP ResolveQuest helper that were never registered as
    // compile-time identities.
    if (!WorldContext) return false;
    const UWorld* World = WorldContext->GetWorld();
    const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    const UQuestStateSubsystem* StateSubsystem = GI ? GI->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    return StateSubsystem && StateSubsystem->HasResolvedWith(QuestTag, OutcomeTag);
}

int32 USimpleQuestBlueprintLibrary::GetQuestCompletionCount(const UObject* WorldContext, const FGameplayTag QuestTag)
{
    UQuestManagerSubsystem* QM = GetQuestManager(WorldContext);
    return QM ? QM->GetQuestCompletionCount(QuestTag) : 0;
}

// -------------------------------------------------------------------------
// Quest actions
// -------------------------------------------------------------------------

void USimpleQuestBlueprintLibrary::DeactivateQuest(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestEventPayload& Payload)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestDeactivateRequest, FQuestDeactivateRequestEvent(QuestTag, EDeactivationSource::External, Payload));
    }
}

void USimpleQuestBlueprintLibrary::GiveQuest(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestObjectiveActivationContext& Params)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestGiven, FQuestGivenEvent(QuestTag, Params));
    }
}

void USimpleQuestBlueprintLibrary::ActivateQuest(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestObjectiveActivationContext& Params)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestActivationRequest, FQuestActivationRequestEvent(QuestTag, Params));
    }
}

void USimpleQuestBlueprintLibrary::SetQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestEventPayload& Payload)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestBlockRequest, FQuestBlockRequestEvent(QuestTag, EDeactivationSource::External, Payload));
    }
}

void USimpleQuestBlueprintLibrary::ClearQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestEventPayload& Payload)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestClearBlockRequest, FQuestClearBlockRequestEvent(QuestTag, EDeactivationSource::External, Payload));
    }
}

void USimpleQuestBlueprintLibrary::ResolveQuest(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag, bool bOverrideExisting, const FQuestEventPayload& Payload)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestResolveRequest, FQuestResolveRequestEvent(QuestTag, OutcomeTag, bOverrideExisting, Payload));
    }
}

void USimpleQuestBlueprintLibrary::StartQuestline(const UObject* WorldContext, TSoftObjectPtr<UQuestlineGraph> QuestlineGraph, const FQuestObjectiveActivationContext& Params)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestlineStartRequest, FQuestlineStartRequestEvent(QuestlineGraph, Params));
    }
}


// -------------------------------------------------------------------------
// Bind to Quest Event
// -------------------------------------------------------------------------

UQuestEventSubscription* USimpleQuestBlueprintLibrary::BindToQuestEvent(UObject* WorldContextObject, FGameplayTag QuestTag, int32 ExposedEvents)
{
    UQuestEventSubscription* Sub = NewObject<UQuestEventSubscription>();
    Sub->InitFromFactory(WorldContextObject, QuestTag, ExposedEvents);
    Sub->RegisterWithGameInstance(WorldContextObject);
    return Sub;
}

void USimpleQuestBlueprintLibrary::UnsubscribeFromQuestEvent(UObject* WorldContextObject, const FGameplayTag& QuestTag, FDelegateHandle Handle)
{
    if (!Handle.IsValid()) return;
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->UnsubscribeMessage(QuestTag, Handle);
    }
}

