// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "SimpleQuestObjectiveSaveState.generated.h"

/**
 * Per-instance objective progress persisted across save/load. Concrete carrier: an objective that carries durable
 * progress fills the fields it uses; the framework stores it keyed by the owning Step's QuestContentGuid and re-applies
 * it after the objective is rebuilt on load. Grows field-by-field as new stateful objective types land (a timer's
 * elapsed seconds, etc.) — the deliberate trade for a Blueprint-native concrete struct over an opaque carrier.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FSimpleQuestObjectiveSaveState
{
	GENERATED_BODY()

	/**
	 * Capture-time signal: true when the objective contributed savable state. Read by the framework to decide whether to
	 * store the entry at all; deliberately NOT SaveGame — every stored entry is implicitly "has state."
	 */
	UPROPERTY(BlueprintReadWrite)
	bool bHasState = false;

	/** Counting-objective progress. Re-applied after the objective is rebuilt on load (the rebuild resets it to 0). */
	UPROPERTY(BlueprintReadWrite, SaveGame)
	int32 CurrentElements = 0;

	/** The objective's completion threshold at save. Re-derived from authored + caller config on rebuild; kept for reference/clamp. */
	UPROPERTY(BlueprintReadWrite, SaveGame)
	int32 MaxElements = 0;
};