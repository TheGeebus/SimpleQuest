// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Events/SignalEventBase.h"
#include "SignalDedupeTestListener.generated.h"

/**
 * Fixture types for SignalDedupeTests. They live in a header rather than beside the tests because a bus subscriber must
 * be a UObject with real member functions - the dispatcher stores a weak pointer and calls through a pointer-to-member -
 * and UnrealHeaderTool only generates reflection for types it finds in headers.
 *
 * NOT wrapped in WITH_DEV_AUTOMATION_TESTS: UHT does not evaluate that symbol, so guarding the UCLASS would leave the
 * generated code referencing a type the compiler had removed. The cost is two empty objects in the shipped module.
 */
USTRUCT()
struct FSignalDedupeTestEvent : public FSignalEventBase
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Value = 0;
};

UCLASS()
class USignalDedupeTestListener : public UObject
{
	GENERATED_BODY()

public:
	/** Two handlers with identical signatures. Their SEPARATENESS is the thing under test: one object binding both is
	 *  two genuine handlers and must be called twice, where one object binding ONE handler twice must be called once. */
	void HandlerA(FGameplayTag MatchedChannel, const FSignalDedupeTestEvent& Event) { ++CallsA; LastChannelA = MatchedChannel; }
	void HandlerB(FGameplayTag MatchedChannel, const FSignalDedupeTestEvent& Event) { ++CallsB; }

	int32 CallsA = 0;
	int32 CallsB = 0;
	FGameplayTag LastChannelA;
};

