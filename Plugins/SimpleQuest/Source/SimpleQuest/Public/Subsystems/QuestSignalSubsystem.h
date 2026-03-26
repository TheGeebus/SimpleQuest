// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "SignalTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "QuestSignalSubsystem.generated.h"

struct FSignalEventBase;

using FQuestEventMulticast = TMulticastDelegate<void(const FInstancedStruct&)>; // C++ multicast delegate

template <typename Base, typename Derived>
concept derived_from = std::is_base_of_v<Base, Derived>;

/**
 * 
 */
UCLASS()
class SIMPLEQUEST_API UQuestSignalSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override; 
	UFUNCTION(BlueprintCallable)
	void SubscribeToQuestEvent(const TSubclassOf<UObject>& QuestClass, const UScriptStruct* Event, UObject* Listener);
	
	UFUNCTION(BlueprintCallable)
	void PublishQuestEvent(const TSubclassOf<UObject>& QuestClass, const FInstancedStruct& Event);

	UFUNCTION(BlueprintCallable)
	void UnsubscribeFromQuestEvent(const TSubclassOf<UObject>& QuestClass, const UScriptStruct* Event, const UObject* Listener);

	template<typename EventType>
	requires derived_from<FSignalEventBase, EventType>
	void PublishTyped(UObject* ChannelObject, const EventType& Event);
	
	template <typename EventType, typename ListenerType>
	requires derived_from<FSignalEventBase, EventType>
	FDelegateHandle SubscribeTyped(UObject* ChannelObject, ListenerType* Listener, void(ListenerType::* Function)(const EventType&));

	template <typename EventType, typename ListenerType>
	requires derived_from<FSignalEventBase, EventType>
	FDelegateHandle SubscribeTypedByTag(FGameplayTag EventTag, ListenerType* Listener, void(ListenerType::* Function)(const EventType&));
	
	template<typename EventType>
	requires derived_from<FSignalEventBase, EventType>
	void UnsubscribeTyped(UObject* ChannelObject, FDelegateHandle Handle);

	void UnsubscribeTypedByTag(const FGameplayTag EventTag, const FDelegateHandle Handle);

private:
	static FSignalEventChannelKey MakeKey(const UObject* Object, const UScriptStruct* Struct);
	TMap<FSignalEventChannelKey, FQuestEventMulticast> NativeQuestEventChannels;
	TMap<FGameplayTag, FQuestEventMulticast> TaggedEventChannels;

	bool bIsShuttingDown = false;
};

template <typename EventType>
requires derived_from<FSignalEventBase, EventType>
void UQuestSignalSubsystem::PublishTyped(UObject* ChannelObject, const EventType& Event)
{
	if (bIsShuttingDown)
	{
		return;
	}
	const FSignalEventChannelKey Channel = MakeKey(ChannelObject, EventType::StaticStruct());
	FInstancedStruct EventCopy = FInstancedStruct::Make<EventType>(Event);	// Copy the event. Prevents a possible crash on PIE shutdown
	if (auto* Delegate = NativeQuestEventChannels.Find(Channel))
	{
		FQuestEventMulticast DelegateCopy = *Delegate;	// Copy the delegate too
		DelegateCopy.Broadcast(EventCopy);
	}
	FGameplayTag CurrentTag = Event.EventTag;
	while (CurrentTag.IsValid())
	{
		if (auto* Delegate = TaggedEventChannels.Find(CurrentTag))
		{
			FQuestEventMulticast DelegateCopy = *Delegate;
			DelegateCopy.Broadcast(EventCopy);
		}
		CurrentTag = CurrentTag.RequestDirectParent();	// Walk up tag hierarchy to allow event listeners to subscribe to any level
	}
}
			
template <typename EventType, typename ListenerType>
requires derived_from<FSignalEventBase, EventType>
FDelegateHandle UQuestSignalSubsystem::SubscribeTyped(UObject* ChannelObject, ListenerType* Listener,
	void(ListenerType::* Function)(const EventType&))
{
	check(IsInGameThread());
	const FSignalEventChannelKey Key = MakeKey(ChannelObject, EventType::StaticStruct());
	auto& Delegate = NativeQuestEventChannels.FindOrAdd(Key);
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
requires derived_from<FSignalEventBase, EventType>
FDelegateHandle UQuestSignalSubsystem::SubscribeTypedByTag(const FGameplayTag EventTag, ListenerType* Listener,
	void(ListenerType::* Function)(const EventType&))
{
	check(IsInGameThread());
	auto& Delegate = TaggedEventChannels.FindOrAdd(EventTag);
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
requires derived_from<FSignalEventBase, EventType>
void UQuestSignalSubsystem::UnsubscribeTyped(UObject* ChannelObject, const FDelegateHandle Handle)
{
	const FSignalEventChannelKey Channel = MakeKey(ChannelObject, EventType::StaticStruct());
	if (TMulticastDelegate<void(const FInstancedStruct&)>* Delegate = NativeQuestEventChannels.Find(Channel))
	{
		Delegate->Remove(Handle);
	}
}


