// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Utilities/SimpleCoreLog.h"
#include "SignalSubsystem.generated.h"

/**
 * One record per Subscribe* call. Handle is the unique unsubscribe key and the deduplication discriminator for
 * multi-channel publishes; Listener is the GC-safe back-reference used to skip stale subscribers without invoking;
 * Dispatcher is the typed unpack closure built at subscription time.
 */
struct FSignalSubscriberRecord
{
    TWeakObjectPtr<UObject> Listener;
    FDelegateHandle Handle;
    TFunction<void(FGameplayTag, const FInstancedStruct&)> Dispatcher;
};

UCLASS()
class SIMPLECORE_API USignalSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    /**
     * Publish a message on Channel. Walks the tag hierarchy from Channel up to root, notifying all subscribers on each ancestor.
     * Visited-set deduplication prevents double-firing at convergence points. Subscribers always receive the original published
     * tag, not the ancestor being walked, so a subscriber on a parent tag, like Quest.Questline1, knows exactly which descendant fired.
     */
    template<typename T>
    void PublishMessage(FGameplayTag Channel, const T& Payload);

    /**
     * Publish a pre-packed FInstancedStruct on Channel. Same tag-hierarchy walk as PublishMessage. Use when forwarding an event
     * received from another subscription without re-packing (avoids type-slicing).
     */
    UFUNCTION(BlueprintCallable)
    void PublishRawMessage(FGameplayTag Channel, const FInstancedStruct& Payload);
    
    /**
     * Publish Event on a set of channels treating the call as one logical event instance. The bus walks each channel's
     * hierarchy independently; subscribers reached via any channel in the set fire exactly once (default dedup-on, by
     * FDelegateHandle), with the callback's first arg set to the channel from the publish set most specific to that
     * subscriber's bound tag (longest channel where the subscriber's tag is an ancestor or equal; tie-break by input
     * array order).
     *
     * Channels route, payloads decide. The payload is packed once and delivered identically across all subscribers — no
     * per-channel mutation, no payload divergence. Identity that subscribers must reliably branch on belongs in the payload
     * (e.g., a publisher-set canonical identity field); the callback's first arg is delivery metadata, not event identity.
     *
     * bAllChannels=true opts out of dedup: bus fires once per channel as a naive sibling-publish would. Payload still
     * identical across deliveries; only the dedup guarantee is dropped. Use for debug tools, observability surfaces, or
     * genuinely-distinct-scope publishes where every channel must reach its own subscribers.
     *
     * Single-channel sets (Channels.Num() == 1) collapse to PublishRawMessage cost — dedup overhead is structurally zero.
     */
    template<typename T>
    void PublishMessageOnChannels(TArray<FGameplayTag> Channels, const T& Event, bool bAllChannels = false);

    /**
     * Raw FInstancedStruct multi-channel publish. Same semantics as the templated form; use when forwarding a payload
     * received from another subscription without re-packing (avoids type-slicing).
     */
    void PublishMessageOnChannelsRaw(TArray<FGameplayTag> Channels, const FInstancedStruct& Payload, bool bAllChannels = false);
    
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
    /**
     * Internal dispatcher for multi-channel publishes. Channels assumed valid and deduplicated upstream, which
     * PublishMessageOnChannelsRaw enforces. Implements the deduplication-on / sibling-publish split based on bAllChannels.
     */
    void DispatchOnChannels(const TArray<FGameplayTag>& Channels, const FInstancedStruct& Payload, bool bAllChannels);

    /** Picks the longest channel from Channels where BoundTag is an ancestor (or equal). Tie-break: array order. */
    static FGameplayTag PickBestMatchChannel(const TArray<FGameplayTag>& Channels, const FGameplayTag& BoundTag);

    TMap<FGameplayTag, TArray<FSignalSubscriberRecord>> ChannelSubscribers;
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

    FSignalSubscriberRecord Record;
    Record.Listener = TWeakObjectPtr<UObject>(Listener);
    Record.Handle = FDelegateHandle{FDelegateHandle::EGenerateNewHandleType::GenerateNewHandle};
    Record.Dispatcher = [WeakListener = TWeakObjectPtr<ListenerType>(Listener), Function]
        (const FGameplayTag ActualChannel, const FInstancedStruct& Struct)
        {
            if (!WeakListener.IsValid()) return;
            if (const T* Typed = Struct.GetPtr<T>())
            {
                (WeakListener.Get()->*Function)(ActualChannel, *Typed);
            }
        };

    const FDelegateHandle Handle = Record.Handle;
    ChannelSubscribers.FindOrAdd(Channel).Add(MoveTemp(Record));
    return Handle;
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

    FSignalSubscriberRecord Record;
    Record.Listener = TWeakObjectPtr<UObject>(Listener);
    Record.Handle = FDelegateHandle{FDelegateHandle::EGenerateNewHandleType::GenerateNewHandle};
    Record.Dispatcher = [WeakListener = TWeakObjectPtr<ListenerType>(Listener), Function]
        (const FGameplayTag ActualChannel, const FInstancedStruct& Struct)
        {
            if (!WeakListener.IsValid()) return;
            if (Struct.GetPtr<T>())
            {
                (WeakListener.Get()->*Function)(ActualChannel, Struct);
            }
        };

    const FDelegateHandle Handle = Record.Handle;
    ChannelSubscribers.FindOrAdd(Channel).Add(MoveTemp(Record));
    return Handle;
}

template<typename T>
void USignalSubsystem::PublishMessageOnChannels(TArray<FGameplayTag> Channels, const T& Event, bool bAllChannels)
{
    if (bIsShuttingDown) return;

    UE_LOG(LogSimpleCore, Verbose, TEXT("Signal::PublishOnChannels: channelCount=%d type='%s' bAllChannels=%s"),
        Channels.Num(),
        *T::StaticStruct()->GetName(),
        bAllChannels ? TEXT("true") : TEXT("false"));

    PublishMessageOnChannelsRaw(MoveTemp(Channels), FInstancedStruct::Make<T>(Event), bAllChannels);
}

