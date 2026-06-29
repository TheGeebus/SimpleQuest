// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestActivationProvenance.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "QuestEntryRecord.generated.h"

/**
 * Single entry event in a quest's start history. One entry appended per RecordEntry call. Append-only;
 * never modified after creation. Designers walking the History array see the full chronological story of
 * every start into this quest — cascade-driven (SourceQuestTag valid), giver-driven (Provenance=GiverGate
 * with ActivationContextSnapshot.Instigator populated), external-API-driven, or initial-entry-fired.
 *
 * The ActivationContextSnapshot field is a by-value copy of the caller's runtime input to the activation —
 * UQuestStep::ReceivedActivationContext.IncomingContext: instigator, custom data, lineage (origin tag / chain /
 * event ID), and any config the caller contributed (target classes / actors, required count, config asset). The
 * Step's own authored config is NOT snapshotted — it re-derives from the Step on replay. Preserved by-value so the
 * entry stays valid after the live UQuestStep is deactivated and ReceivedActivationContext is cleared. Empty
 * default-constructed for non-Step starts (containers have no objective; nothing to snapshot).
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestEntryArrival
{
	GENERATED_BODY()

	/** The upstream quest tag that triggered this entry: the source whose resolution outcome routed into this destination. Invalid for non-cascade starts (giver / external / initial-entry). */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FGameplayTag SourceQuestTag;

	/** The outcome route that fired: the OutcomeTag on the upstream source's resolution that activated this destination. Invalid for non-cascade starts. */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FGameplayTag IncomingOutcomeTag;

	/** World time at this specific entry (GetTimeSeconds on the manager's world). */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	double EntryTime = 0.0;

	/** How this start was initiated. Stamped explicitly at every start site so the registry doesn't infer from sibling-field validity. */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	EQuestActivationProvenance Provenance = EQuestActivationProvenance::Unknown;

	/**
	 * By-value snapshot of the caller's runtime input to this start — UQuestStep::ReceivedActivationContext.IncomingContext:
	 * instigator, custom data, lineage, and any config the caller contributed. The Step's authored config is not captured
	 * (it re-derives from the Step). Empty default-constructed for non-Step starts. Consumed by save/load to reconstitute
	 * the runtime half of live questline state.
	 */
	UPROPERTY(BlueprintReadOnly)
	FQuestObjectiveActivationContext ActivationContextSnapshot;

	/**
	 * Per-source routing identity: the IncomingSourceTag arg threaded through ActivateNodeByTag for duplicate-path disambiguation
	 * on Quest entry filtering. NAME_None for entry-tag fires and any start that didn't carry per-source routing.
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FName PathIdentity = NAME_None;
};

/**
 * Rich-record layer for "what starts have entered this quest, and how." Per-start identity is preserved:
 * cascade source tags, giver actor (via ActivationContextSnapshot.Instigator), provenance, and the caller's
 * runtime input delivered to the objective. Save/load (0.5.0) consumes this directly to reconstitute
 * live questline state.
 *
 * Owned by UQuestStateSubsystem as a TMap<FGameplayTag, FQuestEntryRecord> keyed by destination quest
 * tag. Written via friend access from UQuestManagerSubsystem::HandleOnNodeStarted. Append-only.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestEntryRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame)
	TArray<FQuestEntryArrival> History;

	int32 GetCount() const { return History.Num(); }
	const FQuestEntryArrival* GetLatest() const { return History.IsEmpty() ? nullptr : &History.Last(); }
};