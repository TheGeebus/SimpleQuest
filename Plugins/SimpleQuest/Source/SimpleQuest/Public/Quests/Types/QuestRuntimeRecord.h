// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestRuntimeRecord.generated.h"

/**
 * Top-level per-quest record holding quest-level historical context that isn't tied to an individual start arrival.
 * Per-start detail (giver actor, provenance, merged params snapshot, path identity) lives on FQuestEntryArrival
 * inside the entry registry; per-resolution detail lives on FQuestResolutionEntry. This record holds the data
 * that exists at the quest level — it stamps when the manager first registered the tag, plus future quest-level
 * fields (give-attempt history, cumulative metrics) as they accumulate.
 *
 * Owned by UQuestStateSubsystem as a TMap<FGameplayTag, FQuestRuntimeRecord> keyed by quest tag. The map's keys
 * also answer GetQuestTagsUnderPrefix — every registered quest tag is in the map regardless of whether it has
 * started or resolved yet. Written via friend access from UQuestManagerSubsystem::RegisterQuestlineGraph (mirrors
 * the existing RegisterContainerTag / RecordResolution / RecordEntry / UpdateQuestPrereqStatus push pattern).
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestRuntimeRecord
{
	GENERATED_BODY()

	/**
	 * World time at which the manager first registered this tag (RegisterQuestlineGraph during graph activation).
	 * 0.0 if registered before the manager's world had a valid time source. Useful for telemetry / save-time ordering.
	 */
	UPROPERTY(BlueprintReadOnly)
	double RegisteredTime = 0.0;
};