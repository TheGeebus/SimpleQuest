// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Signals/SignalSubsystem.h"
#include "Events/AbandonQuestEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Quests/QuestNodeBase.h"
#include "Utils/QuestStateTagUtils.h"
#include "GameplayTagsManager.h"
#include "Engine/GameInstance.h"

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

// -------------------------------------------------------------------------
// Quest state queries
// -------------------------------------------------------------------------

bool USimpleQuestBlueprintLibrary::IsQuestActive(const UObject* WorldContext, FGameplayTag QuestTag)
{
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    return WS && WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeStateFact(QuestTag.GetTagName(), QuestStateTagUtils::Leaf_Active), false));
}

bool USimpleQuestBlueprintLibrary::IsQuestCompleted(const UObject* WorldContext, FGameplayTag QuestTag)
{
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    return WS && WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeStateFact(QuestTag.GetTagName(), QuestStateTagUtils::Leaf_Completed), false));
}

bool USimpleQuestBlueprintLibrary::IsQuestPendingGiver(const UObject* WorldContext, FGameplayTag QuestTag)
{
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    return WS && WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeStateFact(QuestTag.GetTagName(), QuestStateTagUtils::Leaf_PendingGiver), false));
}

bool USimpleQuestBlueprintLibrary::IsQuestResolvedWith(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag)
{
    UWorldStateSubsystem* WS = GetWorldState(WorldContext);
    if (!WS || !OutcomeTag.IsValid()) return false;
    return WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeOutcomeFact(OutcomeTag), false));
}

// -------------------------------------------------------------------------
// Quest actions
// -------------------------------------------------------------------------

void USimpleQuestBlueprintLibrary::AbandonQuest(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext)) SS->PublishTyped(UQuestNodeBase::StaticClass(), FAbandonQuestEvent(QuestTag));
}

void USimpleQuestBlueprintLibrary::GiveQuest(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext)) SS->PublishTyped(UQuestNodeBase::StaticClass(), FQuestEnabledEvent(QuestTag, true));
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
