// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestActivationProvenance.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "QuestEntryRecord.generated.h"

/**
 * Single entry event in a quest's start history. One entry appended per RecordEntry call. Append-only;
 * never modified after creation. Designers walking the History array see the full chronological story of
 * every start into this quest — cascade-driven (SourceQuestTag valid), giver-driven (Provenance=GiverGate
 * with ActivationParamsSnapshot.ActivationSource populated), external-API-driven, or initial-entry-fired.
 *
 * The ActivationParamsSnapshot field is a by-value copy of the merged final FQuestObjectiveActivationParams
 * delivered to the objective at activation (UQuestStep::ReceivedActivationParams). Capturing it here means
 * the registry holds enough data to reconstitute the live questline state from a save: target actors / classes,
 * required count, activation source actor, origin chain, custom data, incoming outcome — all preserved by-value
 * so the entry stays valid even after the live UQuestStep has been deactivated and ReceivedActivationParams cleared.
 * Empty default-constructed snapshot for non-Step starts (containers have no objective; nothing to snapshot).
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestEntryArrival
{
	GENERATED_BODY()

	/** The upstream quest tag that triggered this entry: the source whose resolution outcome routed into this destination. Invalid for non-cascade starts (giver / external / initial-entry). */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag SourceQuestTag;

	/** The outcome route that fired: the OutcomeTag on the upstream source's resolution that activated this destination. Invalid for non-cascade starts. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag IncomingOutcomeTag;

	/** World time at this specific entry (GetTimeSeconds on the manager's world). */
	UPROPERTY(BlueprintReadOnly)
	double EntryTime = 0.0;

	/** How this start was initiated. Stamped explicitly at every start site so the registry doesn't infer from sibling-field validity. */
	UPROPERTY(BlueprintReadOnly)
	EQuestActivationProvenance Provenance = EQuestActivationProvenance::Unknown;

	/**
	 * By-value snapshot of the final composed params delivered to the objective at activation (UQuestStep::ReceivedActivationParams).
	 * Captures the full merged input set: authored Step defaults + give-supplied params + cascade origin chain. Empty default-
	 * constructed for non-Step starts (containers have no objective). Used by save/load to fully reconstitute live questline state.
	 */
	UPROPERTY(BlueprintReadOnly)
	FQuestObjectiveActivationParams ActivationParamsSnapshot;

	/**
	 * Per-source routing identity: the IncomingSourceTag arg threaded through ActivateNodeByTag for duplicate-path disambiguation
	 * on Quest entry filtering. NAME_None for entry-tag fires and any start that didn't carry per-source routing.
	 */
	UPROPERTY(BlueprintReadOnly)
	FName PathIdentity = NAME_None;
};

/**
 * Rich-record layer for "what starts have entered this quest, and how." Per-start identity is preserved:
 * cascade source tags, giver actor (via ActivationParamsSnapshot.ActivationSource), provenance, and the
 * full merged params delivered to the objective. Save/load (0.5.0) consumes this directly to reconstitute
 * live questline state.
 *
 * Owned by UQuestStateSubsystem as a TMap<FGameplayTag, FQuestEntryRecord> keyed by destination quest
 * tag. Written via friend access from UQuestManagerSubsystem::HandleOnNodeStarted. Append-only.
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