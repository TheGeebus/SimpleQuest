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
     * Subscribe to messages published on Channel or any of its descendant tags. Callback receives the original published tag
     * and the typed payload. ListenerType must be a UObject subclass (held as TWeakObjectPtr — no manual lifetime management).
     */
    template<typename T, typename ListenerType>
    FDelegateHandle SubscribeMessage(FGameplayTag Channel, ListenerType* Listener, void(ListenerType::* Function)(FGameplayTag, const T&));

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
    
    const FInstancedStruct PackedPayload = FInstancedStruct::Make<T>(Payload);
    TSet<FGameplayTag> VisitedTags;
    FGameplayTag CurrentTag = Channel;

    while (CurrentTag.IsValid())
    {
        if (VisitedTags.Contains(CurrentTag)) break;
        VisitedTags.Add(CurrentTag);

        if (FSignalEventMulticast* Delegate = TagChannels.Find(CurrentTag))
        {
            FSignalEventMulticast DelegateCopy = *Delegate;
            DelegateCopy.Broadcast(Channel, PackedPayload);
        }
        CurrentTag = CurrentTag.RequestDirectParent();
    }
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
