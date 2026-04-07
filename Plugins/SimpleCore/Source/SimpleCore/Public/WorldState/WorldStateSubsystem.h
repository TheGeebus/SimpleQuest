// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Signals/SignalEventBase.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WorldStateSubsystem.generated.h"

USTRUCT(BlueprintType)
struct SIMPLECORE_API FWorldStateFactAddedEvent : public FSignalEventBase
{
	GENERATED_BODY()

	FWorldStateFactAddedEvent() = default;

	explicit FWorldStateFactAddedEvent(const FGameplayTag InStateTag)
		: FSignalEventBase(InStateTag.GetTagName(), InStateTag)
	{}

	UPROPERTY(BlueprintReadWrite)
	FGameplayTag StateTag;
};

USTRUCT(BlueprintType)
struct SIMPLECORE_API FWorldStateFactRemovedEvent : public FSignalEventBase
{
	GENERATED_BODY()

	FWorldStateFactRemovedEvent() = default;

	explicit FWorldStateFactRemovedEvent(const FGameplayTag InStateTag)
		: FSignalEventBase(InStateTag.GetTagName(), InStateTag)
	{}

	UPROPERTY(BlueprintReadWrite)
	FGameplayTag StateTag;
};

UCLASS()
class SIMPLECORE_API UWorldStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void AddFact(FGameplayTag StateTag);

	UFUNCTION(BlueprintCallable)
	void RemoveFact(FGameplayTag StateTag);

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool HasFact(FGameplayTag StateTag) const;

private:
	UPROPERTY()
	FGameplayTagContainer WorldFacts;
};
