// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "BlueprintFunctionLibs/SimpleCoreBlueprintLibrary.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Signals/SignalSubsystem.h"


// ── Signals ────────────────────────────────────────────────────────────────────────────────────────

void USimpleCoreBlueprintLibrary::PublishMessage(UObject* WorldContextObject, FGameplayTag Channel,
    const FInstancedStruct& Payload)
{
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->PublishRawMessage(Channel, Payload);
    }
}

void USimpleCoreBlueprintLibrary::PublishMessageOnChannels(UObject* WorldContextObject,
    const TArray<FGameplayTag>& Channels, const FInstancedStruct& Payload, bool bAllChannels)
{
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->PublishMessageOnChannelsRaw(Channels, Payload, bAllChannels);
    }
}

// ── World State ────────────────────────────────────────────────────────────────────────────────────

void USimpleCoreBlueprintLibrary::AddFact(UObject* WorldContextObject, FGameplayTag Tag,
    EFactBroadcastMode BroadcastMode)
{
    if (UWorldStateSubsystem* WorldState = GetWorldStateSubsystem(WorldContextObject))
    {
        WorldState->AddFact(Tag, BroadcastMode);
    }
}

void USimpleCoreBlueprintLibrary::RemoveFact(UObject* WorldContextObject, FGameplayTag Tag,
    EFactBroadcastMode BroadcastMode)
{
    if (UWorldStateSubsystem* WorldState = GetWorldStateSubsystem(WorldContextObject))
    {
        WorldState->RemoveFact(Tag, BroadcastMode);
    }
}

void USimpleCoreBlueprintLibrary::ClearFact(UObject* WorldContextObject, FGameplayTag Tag, bool bSuppressBroadcast)
{
    if (UWorldStateSubsystem* WorldState = GetWorldStateSubsystem(WorldContextObject))
    {
        WorldState->ClearFact(Tag, bSuppressBroadcast);
    }
}

bool USimpleCoreBlueprintLibrary::HasFact(UObject* WorldContextObject, FGameplayTag Tag)
{
    if (const UWorldStateSubsystem* WorldState = GetWorldStateSubsystem(WorldContextObject))
    {
        return WorldState->HasFact(Tag);
    }
    return false;
}

int32 USimpleCoreBlueprintLibrary::GetFactValue(UObject* WorldContextObject, FGameplayTag Tag)
{
    if (const UWorldStateSubsystem* WorldState = GetWorldStateSubsystem(WorldContextObject))
    {
        return WorldState->GetFactValue(Tag);
    }
    return 0;
}

// ── Resolution helpers ─────────────────────────────────────────────────────────────────────────────

USignalSubsystem* USimpleCoreBlueprintLibrary::GetSignalSubsystem(const UObject* WorldContextObject)
{
    if (!WorldContextObject) return nullptr;
    if (UWorld* World = WorldContextObject->GetWorld())
    {
        if (UGameInstance* GI = World->GetGameInstance())
        {
            return GI->GetSubsystem<USignalSubsystem>();
        }
    }
    return nullptr;
}

UWorldStateSubsystem* USimpleCoreBlueprintLibrary::GetWorldStateSubsystem(const UObject* WorldContextObject)
{
    if (!WorldContextObject) return nullptr;
    if (UWorld* World = WorldContextObject->GetWorld())
    {
        if (UGameInstance* GI = World->GetGameInstance())
        {
            return GI->GetSubsystem<UWorldStateSubsystem>();
        }
    }
    return nullptr;
}