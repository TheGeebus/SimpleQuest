#pragma once

#include "SignalEventBase.generated.h"

USTRUCT(BlueprintType)
struct FSignalEventBase
{
	GENERATED_BODY()

	FSignalEventBase() = default;

	explicit FSignalEventBase(const FName InChannelObjectID)
		: ChannelObjectID(InChannelObjectID) {}
	
	UPROPERTY(BlueprintReadWrite)
	FName ChannelObjectID;
};
