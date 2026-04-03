#pragma once

#include "StructUtils/InstancedStruct.h"
#include "Signals/SignalEventBase.h"
#include "SignalUtilities.generated.h"


UCLASS()
class USignalUtilities : public UObject
{
	GENERATED_BODY()
	
public:
	template <typename EventType>
	static FInstancedStruct CreateSignalEvent(const TSubclassOf<UObject>& ChannelObjectClass)
	{
		static_assert(std::is_base_of_v<FSignalEventBase, EventType>, "Event must derive from FSignalEventBase");
		
		return FInstancedStruct::Make<EventType>(ChannelObjectClass->GetFName());
	}
};