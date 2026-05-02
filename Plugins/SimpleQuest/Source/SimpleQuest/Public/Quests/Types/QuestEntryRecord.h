// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestEntryRecord.generated.h"

/**
 * Single entry event in a quest's entry history. One entry appended per RecordEntry call. Append-only;
 * never modified after creation. Designers walking the History array see the full chronological story of
 * every cascade that triggered an entry into this quest, including which upstream source published the
 * outcome that caused the entry and which outcome route fired.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestEntryArrival
{
	GENERATED_BODY()

	/** The upstream quest tag that triggered this entry: the source whose resolution outcome routed into this destination. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag SourceQuestTag;

	/** The outcome route that fired: the OutcomeTag on the upstream source's resolution that activated this destination. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag IncomingOutcomeTag;

	/** World time at this specific entry (GetTimeSeconds on the manager's world). */
	UPROPERTY(BlueprintReadOnly)
	double EntryTime = 0.0;
};

/**
 * Rich-record layer for "what cascades have entered this quest, and from where." Per-cascade source identity
 * is preserved here: when two upstream sources activate the same destination via the same outcome, the
 * History array carries both records distinctly. The previous WorldState fact was refcount-based and
 * lost source identity (count=2 with no provenance).
 *
 * Owned by UQuestStateSubsystem as a TMap<FGameplayTag, FQuestEntryRecord> keyed by destination quest
 * tag. Written via friend access from UQuestManagerSubsystem::HandleOnNodeStarted (per-cascade during
 * DrainedCascades iteration) and the late-outcome-delivery path in ActivateNodeByTag. Append-only.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestEntryRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TArray<FQuestEntryArrival> History;

	int32 GetCount() const { return History.Num(); }
	const FQuestEntryArrival* GetLatest() const { return History.IsEmpty() ? nullptr : &History.Last(); }
};