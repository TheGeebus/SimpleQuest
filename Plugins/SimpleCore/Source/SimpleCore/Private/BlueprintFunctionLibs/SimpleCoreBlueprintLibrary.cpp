// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "BlueprintFunctionLibs/SimpleCoreBlueprintLibrary.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SignalSubsystem.h"


// ── Signals ────────────────────────────────────────────────────────────────────────────────────────

void USimpleCoreBlueprintLibrary::PublishMessage(UObject* WorldContextObject, FGameplayTag Channel, const FInstancedStruct& Payload)
{
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->PublishRawMessage(Channel, Payload);
    }
}

void USimpleCoreBlueprintLibrary::PublishMessageOnChannels(UObject* WorldContextObject, const TArray<FGameplayTag>& Channels, const FInstancedStruct& Payload, bool bAllChannels)
{
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->PublishMessageOnChannelsRaw(Channels, Payload, bAllChannels);
    }
}

void USimpleCoreBlueprintLibrary::SubscribeMessage(UObject* WorldContextObject, FGameplayTag Channel, const FOnSignalReceived& OnSignalReceived, ESignalRoutingMode Routing)
{
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->SubscribeMessageDynamic(Channel, OnSignalReceived, Routing);
    }
}

void USimpleCoreBlueprintLibrary::SubscribeMessageOfType(UObject* WorldContextObject, FGameplayTag Channel, UScriptStruct* PayloadType, const FOnSignalReceived& OnSignalReceived, ESignalRoutingMode Routing)
{
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->SubscribeMessageOfType(Channel, PayloadType, OnSignalReceived, Routing);
    }
}

void USimpleCoreBlueprintLibrary::UnsubscribeListener(UObject* WorldContextObject, UObject* Listener)
{
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->UnsubscribeListener(Listener);
    }
}

// ── World State ────────────────────────────────────────────────────────────────────────────────────

void USimpleCoreBlueprintLibrary::AddFact(UObject* WorldContextObject, FGameplayTag Tag, EFactBroadcastMode BroadcastMode)
{
    if (UWorldStateSubsystem* WorldState = GetWorldStateSubsystem(WorldContextObject))
    {
        WorldState->AddFact(Tag, BroadcastMode);
    }
}

void USimpleCoreBlueprintLibrary::RemoveFact(UObject* WorldContextObject, FGameplayTag Tag, EFactBroadcastMode BroadcastMode)
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

void USimpleCoreBlueprintLibrary::SubscribeToFactAdded(UObject* WorldContextObject, FGameplayTag Channel,
    const FOnSignalReceived& OnFactAdded, ESignalRoutingMode Routing)
{
    UWorldStateSubsystem* WorldState = GetWorldStateSubsystem(WorldContextObject);
    USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject);
    if (!WorldState || !Signals || !Channel.IsValid() || !OnFactAdded.IsBound()) return;

    // Reuse the store's enumeration rather than re-deriving it here: that is where the snapshot lives (a handler may
    // add or remove facts while consuming this, and iterating the fact map across that is undefined) and where the
    // sort lives (TMap order is unstable, so an unsorted replay would vary run to run).
    TArray<FGameplayTag> AlreadyTrue;
    WorldState->GetFactsMatching(Channel, Routing, AlreadyTrue);

    // Catch-up before wiring, matching the C++ path exactly. Each delivery passes the specific fact's tag as the
    // matched channel, so a graph bound at a parent tag reads the same pin value it would on a live hit.
    for (const FGameplayTag& FactTag : AlreadyTrue)
    {
        OnFactAdded.ExecuteIfBound(FactTag,
            FInstancedStruct::Make<FWorldStateFactAddedEvent>(FWorldStateFactAddedEvent(FactTag, /*bCatchUp*/ true)));
    }

    Signals->SubscribeMessageOfType(Channel, FWorldStateFactAddedEvent::StaticStruct(), OnFactAdded, Routing);
}

// ── Gameplay Tags ──────────────────────────────────────────────────────────────────────────────

FGameplayTag USimpleCoreBlueprintLibrary::GetDirectParentTag(FGameplayTag Tag)
{
    return Tag.RequestDirectParent();
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