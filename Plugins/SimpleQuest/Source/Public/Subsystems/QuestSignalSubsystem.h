// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "SignalTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
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
	void SubscribeToQuestEvent(const TSubclassOf<UObject>& QuestClass, const UScriptStruct* EventClass, UObject* Listener);
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

	template<typename EventType>
	requires derived_from<FSignalEventBase, EventType>
	void UnsubscribeTyped(UObject* ChannelObject, FDelegateHandle Handle);

private:
	static FSignalEventChannelKey MakeKey(const UObject* Object, const UScriptStruct* Struct);
	TMap<FSignalEventChannelKey, FQuestEventMulticast> NativeQuestEventChannels;

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
	if (auto* Delegate = NativeQuestEventChannels.Find(Channel))
	{
		FQuestEventMulticast DelegateCopy = *Delegate;		// Copy the delegate and event. Don't like doing this, but it prevents a race condition on PIE shutdown
		FInstancedStruct EventCopy = FInstancedStruct::Make<EventType>(Event);
		DelegateCopy.Broadcast(EventCopy);
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

template <typename EventType>
requires derived_from<FSignalEventBase, EventType>
void UQuestSignalSubsystem::UnsubscribeTyped(UObject* ChannelObject, FDelegateHandle Handle)
{
	const FSignalEventChannelKey Channel = MakeKey(ChannelObject, EventType::StaticStruct());
	if (TMulticastDelegate<void(const FInstancedStruct&)>* Delegate = NativeQuestEventChannels.Find(Channel))
	{
		Delegate->Remove(Handle);
	}
}
