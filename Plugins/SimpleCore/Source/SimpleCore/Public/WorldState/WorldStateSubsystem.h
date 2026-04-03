// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Signals/SignalEventBase.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WorldStateSubsystem.generated.h"

USTRUCT(BlueprintType)
struct SIMPLECORE_API FStateSatisfiedEvent : public FSignalEventBase
{
	GENERATED_BODY()

	FStateSatisfiedEvent() = default;

	explicit FStateSatisfiedEvent(const FGameplayTag InStateTag)
		: FSignalEventBase(InStateTag.GetTagName()), StateTag(InStateTag)
	{
		EventTag = InStateTag;
	}

	UPROPERTY(BlueprintReadWrite)
	FGameplayTag StateTag;
};

USTRUCT(BlueprintType)
struct SIMPLECORE_API FStateUnsatisfiedEvent : public FSignalEventBase
{
	GENERATED_BODY()

	FStateUnsatisfiedEvent() = default;

	explicit FStateUnsatisfiedEvent(const FGameplayTag InStateTag)
		: FSignalEventBase(InStateTag.GetTagName()), StateTag(InStateTag)
	{
		EventTag = InStateTag;
	}

	UPROPERTY(BlueprintReadWrite)
	FGameplayTag StateTag;
};

UCLASS()
class SIMPLECORE_API UWorldStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void SatisfyState(FGameplayTag StateTag);

	UFUNCTION(BlueprintCallable)
	void UnsatisfyState(FGameplayTag StateTag);

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsStateSatisfied(FGameplayTag StateTag) const;

private:
	UPROPERTY()
	FGameplayTagContainer SatisfiedStateTags;
};
