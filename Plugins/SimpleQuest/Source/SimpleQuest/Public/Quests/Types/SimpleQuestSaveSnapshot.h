// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "Quests/Types/QuestEntryRecord.h"
#include "Quests/Types/QuestObjectiveRuntimeContext.h"
#include "Quests/Types/SimpleQuestObjectiveSaveState.h"
#include "SimpleQuestSaveSnapshot.generated.h"

/**
 * Serializable snapshot of all SimpleQuest runtime state, captured via UQuestStateSubsystem::CaptureSnapshot
 * and restored via ApplySnapshot. Consumers embed this in their own USaveGame (a monolithic game save or a
 * dedicated quest one) - the plugin ships no USaveGame subclass. Fields here carry the SaveGame flag to record
 * what is meant to persist; note that the engine's default save path writes every non-transient property whether
 * or not the flag is present, so it cannot be used to keep something out of a save.
 *
 * Layers captured: WorldState facts (the primary state store) and the QSS resolution + entry history registries,
 * including each entry's ActivationParamsSnapshot. The parallel lookup indices (ResolvedOutcomesByQuest, etc.)
 * are NOT stored - they rebuild from the histories on apply.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FSimpleQuestSaveSnapshot
{
	GENERATED_BODY()

	/** Current schema version. Bump on incompatible layout changes; ApplySnapshot branches on Version. */
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY(SaveGame)
	int32 Version = CurrentVersion;

	/** Layer 1 - UWorldStateSubsystem fact map (running facts, completion, blocked, prereq satisfaction, …). */
	UPROPERTY(SaveGame)
	TMap<FGameplayTag, int32> WorldFacts;

	/** Layer 2 - QSS resolution registry (per-quest outcome history). Outcome/path indices rebuilt on apply. */
	UPROPERTY(SaveGame)
	TMap<FGameplayTag, FQuestResolutionRecord> Resolutions;

	/** Layer 2 - QSS entry registry (per-quest start history). Outcome index rebuilt on apply. */
	UPROPERTY(SaveGame)
	TMap<FGameplayTag, FQuestEntryRecord> Entries;
	
	/**
	 * Soft paths of the questline graphs that were in play when this snapshot was captured. Makes the save self-
	 * describing about what to restore: RestoreQuestState walks this list and restores each graph, so the caller never
	 * has to enumerate graph assets on load. Captured from the manager's registered + reachability-warmed set (a safe
	 * superset - a dormant graph restores to a no-op). Empty for snapshots captured before this field existed; such a
	 * save still applies its facts + history, the caller just drives graph restore itself via RestoreQuestline.
	 */
	UPROPERTY(SaveGame)
	TArray<FSoftObjectPath> ActiveGraphs;
	
	/**
	 * Nodes that were armed and WAITING on a prerequisite at capture time - a prereq-deferred activation that hasn't
	 * fired yet (a chapter gated on the previous one, a standalone Prereq Gate, etc.). Keyed by QuestContentGuid (the
	 * per-placement save key - see its contract on UQuestNodeBase; resolved to nodes via the compiled graph, not by
	 * inverting the GUID), valid for content nodes AND tag-less utility nodes alike, mapping to the runtime context to
	 * activate with. This "waiting" state is transient runtime state, not a fact, so without it a deferred node never
	 * wakes on restore even when its prereq later satisfies. RestoreQuestlineGraph replays Activate for each entry,
	 * which re-defers (or fires if the prereq is already met).
	 */
	UPROPERTY(SaveGame)
	TMap<FGuid, FQuestObjectiveRuntimeContext> DeferredActivations;
	
	/**
	 * Per-instance objective progress (e.g. a counting objective's CurrentElements), keyed by the owning Step's
	 * QuestContentGuid (the per-placement save key - see its contract on UQuestNodeBase; resolved via the compiled
	 * graph, not by inverting the GUID). Captured from each live objective's CaptureObjectiveState; re-applied after
	 * the objective is rebuilt on load. Only objectives that contributed state appear here.
	 */
	UPROPERTY(SaveGame)
	TMap<FGuid, FSimpleQuestObjectiveSaveState> ObjectiveStates;
};