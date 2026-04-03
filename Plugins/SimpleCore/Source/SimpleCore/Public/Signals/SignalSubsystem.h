// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "Signals/SignalTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "SignalSubsystem.generated.h"

struct FSignalEventBase;

using FSignalEventMulticast = TMulticastDelegate<void(const FInstancedStruct&)>;

template <typename Base, typename Derived>
concept derived_from_base = std::is_base_of_v<Base, Derived>;

UCLASS()
class SIMPLECORE_API USignalSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    template<typename EventType>
    requires derived_from_base<FSignalEventBase, EventType>
    void PublishTyped(UObject* ChannelObject, const EventType& Event);

    template <typename EventType, typename ListenerType>
    requires derived_from_base<FSignalEventBase, EventType>
    FDelegateHandle SubscribeTyped(UObject* ChannelObject, ListenerType* Listener, void(ListenerType::* Function)(const EventType&));

    template <typename EventType, typename ListenerType>
    requires derived_from_base<FSignalEventBase, EventType>
    FDelegateHandle SubscribeTypedByTag(FGameplayTag EventTag, ListenerType* Listener, void(ListenerType::* Function)(const EventType&));

    template<typename EventType>
    requires derived_from_base<FSignalEventBase, EventType>
    void UnsubscribeTyped(UObject* ChannelObject, FDelegateHandle Handle);

    void UnsubscribeTypedByTag(FGameplayTag EventTag, FDelegateHandle Handle);

private:
    static FSignalEventChannelKey MakeKey(const UObject* Object, const UScriptStruct* Struct);
    TMap<FSignalEventChannelKey, FSignalEventMulticast> ObjectChannels;
    TMap<FGameplayTag, FSignalEventMulticast> TagChannels;

    bool bIsShuttingDown = false;
};

template <typename EventType>
requires derived_from_base<FSignalEventBase, EventType>
void USignalSubsystem::PublishTyped(UObject* ChannelObject, const EventType& Event)
{
    if (bIsShuttingDown) return;

    const FSignalEventChannelKey Channel = MakeKey(ChannelObject, EventType::StaticStruct());
    FInstancedStruct EventCopy = FInstancedStruct::Make<EventType>(Event);

    if (auto* Delegate = ObjectChannels.Find(Channel))
    {
        FSignalEventMulticast DelegateCopy = *Delegate;
        DelegateCopy.Broadcast(EventCopy);
    }

    FGameplayTag CurrentTag = Event.EventTag;
    while (CurrentTag.IsValid())
    {
        if (auto* Delegate = TagChannels.Find(CurrentTag))
        {
            FSignalEventMulticast DelegateCopy = *Delegate;
            DelegateCopy.Broadcast(EventCopy);
        }
        CurrentTag = CurrentTag.RequestDirectParent();
    }
}

template <typename EventType, typename ListenerType>
requires derived_from_base<FSignalEventBase, EventType>
FDelegateHandle USignalSubsystem::SubscribeTyped(UObject* ChannelObject, ListenerType* Listener,
    void(ListenerType::* Function)(const EventType&))
{
    check(IsInGameThread());
    const FSignalEventChannelKey Key = MakeKey(ChannelObject, EventType::StaticStruct());
    auto& Delegate = ObjectChannels.FindOrAdd(Key);
    return Delegate.AddLambda(
    [WeakListener = TWeakObjectPtr<ListenerType>(Listener), Function](const FInstancedStruct& Struct)
    {
        if (!WeakListener.IsValid()) return;
        if (const EventType* Event = Struct.GetPtr<EventType>())
        {
            (WeakListener.Get()->*Function)(*Event);
        }
    });
}

template <typename EventType, typename ListenerType>
requires derived_from_base<FSignalEventBase, EventType>
FDelegateHandle USignalSubsystem::SubscribeTypedByTag(const FGameplayTag EventTag, ListenerType* Listener,
    void(ListenerType::* Function)(const EventType&))
{
    check(IsInGameThread());
    auto& Delegate = TagChannels.FindOrAdd(EventTag);
    return Delegate.AddLambda(
    [WeakListener = TWeakObjectPtr<ListenerType>(Listener), Function](const FInstancedStruct& Struct)
    {
        if (!WeakListener.IsValid()) return;
        if (const EventType* Event = Struct.GetPtr<EventType>())
        {
            (WeakListener.Get()->*Function)(*Event);
        }
    });
}

template <typename EventType>
requires derived_from_base<FSignalEventBase, EventType>
void USignalSubsystem::UnsubscribeTyped(UObject* ChannelObject, const FDelegateHandle Handle)
{
    const FSignalEventChannelKey Channel = MakeKey(ChannelObject, EventType::StaticStruct());
    if (FSignalEventMulticast* Delegate = ObjectChannels.Find(Channel))
    {
        Delegate->Remove(Handle);
    }
}
