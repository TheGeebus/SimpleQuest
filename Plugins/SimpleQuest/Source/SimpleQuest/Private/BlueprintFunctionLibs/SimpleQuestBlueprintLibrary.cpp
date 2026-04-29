// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
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
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    return WS && WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live), false));
}

bool USimpleQuestBlueprintLibrary::IsQuestCompleted(const UObject* WorldContext, FGameplayTag QuestTag)
{
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    return WS && WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Completed), false));
}

bool USimpleQuestBlueprintLibrary::IsQuestPendingGiver(const UObject* WorldContext, FGameplayTag QuestTag)
{
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    return WS && WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver), false));
}

bool USimpleQuestBlueprintLibrary::IsQuestResolvedWith(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag)
{
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    if (!WS || !OutcomeTag.IsValid()) return false;
    return WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(FQuestStateTagUtils::MakeOutcomeFact(OutcomeTag), false));
}

int32 USimpleQuestBlueprintLibrary::GetQuestCompletionCount(const UObject* WorldContext, const FGameplayTag QuestTag)
{
    UQuestManagerSubsystem* QM = GetQuestManager(WorldContext);
    return QM ? QM->GetQuestCompletionCount(QuestTag) : 0;
}

// -------------------------------------------------------------------------
// Quest actions
// -------------------------------------------------------------------------

void USimpleQuestBlueprintLibrary::DeactivateQuest(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestDeactivateRequest, FQuestDeactivateRequestEvent(QuestTag, EDeactivationSource::External));
    }
}

void USimpleQuestBlueprintLibrary::GiveQuest(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestGiven, FQuestGivenEvent(QuestTag));
    }
}

void USimpleQuestBlueprintLibrary::ActivateQuest(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestActivationRequest, FQuestActivationRequestEvent(QuestTag, FQuestObjectiveActivationParams()));
    }
}

void USimpleQuestBlueprintLibrary::SetQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestBlockRequest, FQuestBlockRequestEvent(QuestTag));
    }
}

void USimpleQuestBlueprintLibrary::ClearQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestClearBlockRequest, FQuestClearBlockRequestEvent(QuestTag));
    }
}

void USimpleQuestBlueprintLibrary::ResolveQuest(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag, bool bOverrideExisting)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestResolveRequest, FQuestResolveRequestEvent(QuestTag, OutcomeTag, bOverrideExisting));
    }
}

void USimpleQuestBlueprintLibrary::StartQuestline(const UObject* WorldContext, TSoftObjectPtr<UQuestlineGraph> QuestlineGraph)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestlineStartRequest, FQuestlineStartRequestEvent(QuestlineGraph));
    }
}

// -------------------------------------------------------------------------
// World state
// -------------------------------------------------------------------------

void USimpleQuestBlueprintLibrary::AddWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag)
{
    if (UWorldStateSubsystem* WS = GetWorldState(WorldContext)) WS->AddFact(FactTag);
}

void USimpleQuestBlueprintLibrary::RemoveWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag)
{
    if (UWorldStateSubsystem* WS = GetWorldState(WorldContext)) WS->RemoveFact(FactTag);
}

bool USimpleQuestBlueprintLibrary::HasWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag)
{
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    return WS && WS->HasFact(FactTag);
}

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

