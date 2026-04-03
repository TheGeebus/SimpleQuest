#pragma once

#include "GameplayTagContainer.h"
#include "SignalEventBase.generated.h"

USTRUCT(BlueprintType)
struct SIMPLECORE_API FSignalEventBase
{
	GENERATED_BODY()

	FSignalEventBase() = default;

	explicit FSignalEventBase(const FName InChannelObjectID)
		: ChannelObjectID(InChannelObjectID) {}
	
	UPROPERTY(BlueprintReadWrite)
	FName ChannelObjectID;
	
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag EventTag;
};
