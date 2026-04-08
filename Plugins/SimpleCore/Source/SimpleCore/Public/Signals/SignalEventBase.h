#pragma once

#include "GameplayTagContainer.h"
#include "SignalEventBase.generated.h"

USTRUCT(BlueprintType)
struct SIMPLECORE_API FSignalEventBase
{
	GENERATED_BODY()

	FSignalEventBase() = default;

	explicit FSignalEventBase(FGameplayTag InEventTag)
	{
		EventTags.AddTag(InEventTag);
	}

	UPROPERTY(BlueprintReadWrite)
	FGameplayTagContainer EventTags;
};
