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

template <typename Derived, typename Base>
concept derived_from_base = std::is_base_of_v<Base, Derived>;

UCLASS()
class SIMPLECORE_API USignalSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    template<typename EventType>
    requires derived_from_base<EventType, FSignalEventBase>
    void PublishTyped(UObject* ChannelObject, const EventType& Event);

    template <typename EventType, typename ListenerType>
    requires derived_from_base<EventType, FSignalEventBase>
    FDelegateHandle SubscribeTyped(UObject* ChannelObject, ListenerType* Listener, void(ListenerType::* Function)(const EventType&));

    template <typename EventType, typename ListenerType>
    requires derived_from_base<EventType, FSignalEventBase>
    FDelegateHandle SubscribeTypedByTag(FGameplayTag EventTag, ListenerType* Listener, void(ListenerType::* Function)(const EventType&));

    template<typename EventType>
    requires derived_from_base<EventType, FSignalEventBase>
    void UnsubscribeTyped(UObject* ChannelObject, FDelegateHandle Handle);

    void UnsubscribeByTag(FGameplayTag EventTag, FDelegateHandle Handle);

private:
    static FSignalEventChannelKey MakeKey(const UObject* Object, const UScriptStruct* Struct);
    TMap<FSignalEventChannelKey, FSignalEventMulticast> ObjectChannels;
    TMap<FGameplayTag, FSignalEventMulticast> TagChannels;

    bool bIsShuttingDown = false;
};

template <typename EventType>
requires derived_from_base<EventType, FSignalEventBase>
void USignalSubsystem::PublishTyped(UObject* ChannelObject, const EventType& Event)
{
    if (bIsShuttingDown) return;

    // Legacy broadcast method with channels keyed by UObject/UScriptStruct pairs. Currently preserved for backwards compatibility.
    // Will be removed following transition to tags as event identifiers
    const FSignalEventChannelKey Channel = MakeKey(ChannelObject, EventType::StaticStruct());
    FInstancedStruct EventCopy = FInstancedStruct::Make<EventType>(Event);

    if (auto* Delegate = ObjectChannels.Find(Channel))
    {
        FSignalEventMulticast DelegateCopy = *Delegate;
        DelegateCopy.Broadcast(EventCopy);
    }

    // Events hold EventTags - FGameplayTagContainers - because linked questline graphs may have subscribers at both the parent and
    // child levels. So we store a tag that describes the whole questline hierarchy from the top parent level down to this event,
    // and we also store separate tags that describe the hierarchy from the start of each linked child graph in the chain without its
    // parent context. Subscribers may then subscribe to specific instances of an event in some graph which is reused in some chain of
    // linked graphs or to all instances of the event throughout that chain as needed.
    //
    // We want to walk all tag hierarchies from the tip to root, checking if we've already broadcast this exact event because tag
    // hierarchies examined starting from the tip may have duplicates that represent graph links.
    
    TSet<FGameplayTag> VisitedTags;
    for (const FGameplayTag& RootTag : Event.EventTags)
    {
        FGameplayTag CurrentTag = RootTag;
        while (CurrentTag.IsValid())
        {
            if (VisitedTags.Contains(CurrentTag)) break; // Detected common ancestry with a branch we've already walked up, so this tag and any parent tags in this hierarchy are duplicates. Stop walking this branch and move to the next.
            VisitedTags.Add(CurrentTag);

            if (auto* Delegate = TagChannels.Find(CurrentTag))
            {
                FSignalEventMulticast DelegateCopy = *Delegate;
                DelegateCopy.Broadcast(EventCopy);
            }
            CurrentTag = CurrentTag.RequestDirectParent(); // Inner 'while' loop walks up the hierarchy from tip to root, iterating outer 'for' loop to next root in tag container when done
        }
    }

}

template <typename EventType, typename ListenerType>
requires derived_from_base<EventType, FSignalEventBase>
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
requires derived_from_base<EventType, FSignalEventBase>
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
requires derived_from_base<EventType, FSignalEventBase>
void USignalSubsystem::UnsubscribeTyped(UObject* ChannelObject, const FDelegateHandle Handle)
{
    const FSignalEventChannelKey Channel = MakeKey(ChannelObject, EventType::StaticStruct());
    if (FSignalEventMulticast* Delegate = ObjectChannels.Find(Channel))
    {
        Delegate->Remove(Handle);
    }
}
