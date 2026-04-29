// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestResolutionRecord.generated.h"

/**
 * Origin of a quest resolution: graph-driven (natural completion via ChainToNextNodes) or external
 * (USimpleQuestBlueprintLibrary::ResolveQuest, save-system rehydration). Carried on every FQuestResolutionEntry
 * so the rich-record history preserves audit info without separate WorldState marker facts. Mirrored on
 * FQuestEndedEvent so live subscribers see the same audit info without a registry query.
 */
UENUM(BlueprintType)
enum class EQuestResolutionSource : uint8
{
	Graph		UMETA(DisplayName = "Graph"),
	External	UMETA(DisplayName = "External"),
};

/**
 * Single resolution event in a quest's history. One entry appended per RecordResolution call. Append-only;
 * never modified after creation. Designers walking the History array see the full chronological story of every
 * outcome a quest fired across the session.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestResolutionEntry
{
	GENERATED_BODY()

	/**
	 * The outcome the quest resolved with on this specific occurrence. May be invalid for "resolve without
	 * specifying an outcome" calls via the BP ResolveQuest helper.
	 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag OutcomeTag;

	/** World time at this specific resolution (GetTimeSeconds on the manager's world). */
	UPROPERTY(BlueprintReadOnly)
	double ResolutionTime = 0.0;

	/** Whether this resolution was fired through the graph or via an external call. */
	UPROPERTY(BlueprintReadOnly)
	EQuestResolutionSource Source = EQuestResolutionSource::Graph;
};

/**
 * Rich-record layer companion to WorldState's boolean QuestState.<X>.Completed fact. WorldState answers
 * "did this quest resolve at all?" in O(1) via fact presence. This record answers "what is the full resolution
 * history" with a chronologically ordered FQuestResolutionEntry per call to RecordResolution.
 *
 * Owned by UQuestStateSubsystem as a TMap<FGameplayTag, FQuestResolutionRecord> keyed by quest tag. Written
 * atomically alongside the WorldState facts in UQuestManagerSubsystem::SetQuestResolved, we should never touch one
 * layer without the other. Append-only: every resolution adds an entry; no mutation, no overwrite.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestResolutionRecord
{
	GENERATED_BODY()

	/**
	 * Full chronological history of every resolution this quest fired. Append-only; one entry per
	 * RecordResolution call. Designers walking this array see the complete story across the session.
	 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FQuestResolutionEntry> History;

	/** Convenience: count of total resolutions across the session. Equivalent to History.Num(). */
	int32 GetCount() const { return History.Num(); }

	/** Convenience: most recent resolution entry, or nullptr if no resolutions yet. */
	const FQuestResolutionEntry* GetLatest() const { return History.IsEmpty() ? nullptr : &History.Last(); }
};