// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"

#include "SimpleQuestLog.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Subsystems/SignalSubsystem.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "BlueprintAsync/QuestLifecycleObserver.h"
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
#include "Subsystems/QuestStateSubsystem.h"
#include "Objectives/QuestObjective.h"


// -------------------------------------------------------------------------
// Private helpers
// -------------------------------------------------------------------------

UWorldStateSubsystem* USimpleQuestBlueprintLibrary::GetWorldStateSubsystem(const UObject* WorldContext)
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

UQuestManagerSubsystem* USimpleQuestBlueprintLibrary::GetQuestManagerSubsystem(const UObject* WorldContext)
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
    return FQuestLifecycleQuery::IsLive(GetWorldStateSubsystem(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestCompleted(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsCompleted(GetWorldStateSubsystem(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestPendingGiver(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsPendingGiver(GetWorldStateSubsystem(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsBlocked(GetWorldStateSubsystem(WorldContext), QuestTag);
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
    UQuestManagerSubsystem* QM = GetQuestManagerSubsystem(WorldContext);
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

void USimpleQuestBlueprintLibrary::ActivateQuest(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestObjectiveActivationContext& Params, bool bBypassPrerequisites)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestActivationRequest, FQuestActivationRequestEvent(QuestTag, Params, bBypassPrerequisites));
    }
}

void USimpleQuestBlueprintLibrary::SetQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestEventPayload& Payload, bool bAlsoDeactivate)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestBlockRequest, FQuestBlockRequestEvent(QuestTag, EDeactivationSource::External, Payload, bAlsoDeactivate));
    }
}

void USimpleQuestBlueprintLibrary::ClearQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestEventPayload& Payload)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestClearBlockRequest, FQuestClearBlockRequestEvent(QuestTag, EDeactivationSource::External, Payload));
    }
}

void USimpleQuestBlueprintLibrary::ResetQuestRunState(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext))
    {
        Manager->ResetQuestRunState(QuestTag);
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

void USimpleQuestBlueprintLibrary::LogSimpleQuestMessage(const FString& Message, EQuestLogLevel Level)
{
    switch (Level)
    {
    case EQuestLogLevel::Error:   UE_LOG(LogSimpleQuest, Error,   TEXT("%s"), *Message); break;
    case EQuestLogLevel::Display: UE_LOG(LogSimpleQuest, Display, TEXT("%s"), *Message); break;
    case EQuestLogLevel::Verbose: UE_LOG(LogSimpleQuest, Verbose, TEXT("%s"), *Message); break;
    case EQuestLogLevel::Warning:
    default:                      UE_LOG(LogSimpleQuest, Warning, TEXT("%s"), *Message); break;
    }
}


// -------------------------------------------------------------------------
// Observe Quest Lifecycle
// -------------------------------------------------------------------------

UQuestLifecycleObserver* USimpleQuestBlueprintLibrary::ObserveQuestLifecycle(UObject* WorldContextObject, FGameplayTag QuestTag, int32 ExposedEvents, ESignalRoutingMode Routing)
{
    UQuestLifecycleObserver* Sub = NewObject<UQuestLifecycleObserver>();
    Sub->InitFromFactory(WorldContextObject, QuestTag, ExposedEvents, Routing);
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

TArray<FQuestRoleSourceInfo> USimpleQuestBlueprintLibrary::GetActiveTriggersForTag(const UObject* WorldContext, FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetActiveTriggersForTag(QueryTag);
    }
    return {};
}

TArray<FQuestRoleSourceInfo> USimpleQuestBlueprintLibrary::GetActiveGiversForTag(const UObject* WorldContext, FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetActiveGiversForTag(QueryTag);
    }
    return {};
}

TArray<FQuestRoleSourceInfo> USimpleQuestBlueprintLibrary::GetActiveObserversForTag(const UObject* WorldContext, FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetActiveObserversForTag(QueryTag);
    }
    return {};
}

UQuestObjective* USimpleQuestBlueprintLibrary::GetActiveObjectiveForTag(const UObject* WorldContext, FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetActiveObjectiveForTag(QueryTag);
    }
    return nullptr;
}

FText USimpleQuestBlueprintLibrary::GetQuestDisplayNameText(const UObject* WorldContext, const FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetDisplayName(QueryTag);
    }
    return {};
}

FText USimpleQuestBlueprintLibrary::GetQuestDescriptionText(const UObject* WorldContext, const FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetDisplayDescription(QueryTag);
    }
    return {};
}

UQuestDisplayData* USimpleQuestBlueprintLibrary::GetQuestDisplayDataAsset(const UObject* WorldContext, const FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetDisplayData(QueryTag);
    }
    return nullptr;
}

UQuestStateSubsystem* USimpleQuestBlueprintLibrary::GetQuestStateSubsystem(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    const UWorld* World = WorldContext->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UQuestStateSubsystem>() : nullptr;
}

