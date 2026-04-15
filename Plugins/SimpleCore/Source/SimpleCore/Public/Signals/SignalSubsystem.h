// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Utilities/SimpleCoreLog.h"
#include "SignalSubsystem.generated.h"

// Internal multicast delegate. Payload is type-erased; first arg is the original published channel tag.
using FSignalEventMulticast = TMulticastDelegate<void(FGameplayTag, const FInstancedStruct&)>;

UCLASS()
class SIMPLECORE_API USignalSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    /**
     * Publish a message on Channel. Walks the tag hierarchy from Channel up to root, notifying all subscribers on each ancestor.
     * Visited-set deduplication prevents double-firing at convergence points. Subscribers always receive the original published
     * tag, not the ancestor being walked — a subscriber on Quest.Questline1 knows exactly which descendant fired.
     */
    template<typename T>
    void PublishMessage(FGameplayTag Channel, const T& Payload);

    /**
     * Publish a pre-packed FInstancedStruct on Channel. Same tag-hierarchy walk as PublishMessage. Use when forwarding an event
     * received from another subscription without re-packing (avoids type-slicing).
     */
    void PublishRawMessage(FGameplayTag Channel, const FInstancedStruct& Payload);
    
    /**
     * Subscribe to messages published on Channel or any of its descendant tags. Callback receives the original published tag
     * and the typed payload. ListenerType must be a UObject subclass (held as TWeakObjectPtr — no manual lifetime management).
     */
    template<typename T, typename ListenerType>
    FDelegateHandle SubscribeMessage(FGameplayTag Channel, ListenerType* Listener, void(ListenerType::* Function)(FGameplayTag, const T&));

    /**
     * Subscribe to messages on Channel where the payload IS-A T. Callback receives the original FInstancedStruct preserving the
     * concrete derived type. Use when the subscriber needs access to subclass fields without knowing the concrete type at compile time.
     */
    template<typename T, typename ListenerType>
    FDelegateHandle SubscribeRawMessage(FGameplayTag Channel, ListenerType* Listener, void(ListenerType::* Function)(FGameplayTag, const FInstancedStruct&));
    
    /** Remove a subscription by the handle returned from SubscribeMessage. */
    void UnsubscribeMessage(FGameplayTag Channel, FDelegateHandle Handle);

private:
    TMap<FGameplayTag, FSignalEventMulticast> TagChannels;
    bool bIsShuttingDown = false;
};

template<typename T>
void USignalSubsystem::PublishMessage(const FGameplayTag Channel, const T& Payload)
{
    if (bIsShuttingDown) return;

    UE_LOG(LogSimpleCore, Verbose, TEXT("Signal::Publish: channel='%s' type='%s'"),
        *Channel.ToString(),
        *T::StaticStruct()->GetName());

    PublishRawMessage(Channel, FInstancedStruct::Make<T>(Payload));
}

template<typename T, typename ListenerType>
FDelegateHandle USignalSubsystem::SubscribeMessage(const FGameplayTag Channel, ListenerType* Listener,
    void(ListenerType::* Function)(FGameplayTag, const T&))
{
    check(IsInGameThread());

    UE_LOG(LogSimpleCore, Verbose, TEXT("Signal::Subscribe: channel='%s' listener='%s'"),
        *Channel.ToString(),
        Listener ? *Listener->GetName() : TEXT("null"));
    
    auto& Delegate = TagChannels.FindOrAdd(Channel);
    return Delegate.AddLambda(
        [WeakListener = TWeakObjectPtr<ListenerType>(Listener), Function]
        (const FGameplayTag ActualChannel, const FInstancedStruct& Struct)
        {
            if (!WeakListener.IsValid()) return;
            if (const T* Typed = Struct.GetPtr<T>())
            {
                (WeakListener.Get()->*Function)(ActualChannel, *Typed);
            }
        });
}

template<typename T, typename ListenerType>
FDelegateHandle USignalSubsystem::SubscribeRawMessage(const FGameplayTag Channel, ListenerType* Listener,
    void(ListenerType::* Function)(FGameplayTag, const FInstancedStruct&))
{
    check(IsInGameThread());

    UE_LOG(LogSimpleCore, Verbose, TEXT("Signal::SubscribeRaw: channel='%s' filter='%s' listener='%s'"),
        *Channel.ToString(),
        *T::StaticStruct()->GetName(),
        Listener ? *Listener->GetName() : TEXT("null"));

    auto& Delegate = TagChannels.FindOrAdd(Channel);
    return Delegate.AddLambda(
        [WeakListener = TWeakObjectPtr<ListenerType>(Listener), Function]
        (const FGameplayTag ActualChannel, const FInstancedStruct& Struct)
        {
            if (!WeakListener.IsValid()) return;
            if (Struct.GetPtr<T>())
            {
                (WeakListener.Get()->*Function)(ActualChannel, Struct);
            }
        });
}
