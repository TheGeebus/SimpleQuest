// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "Quests/Types/QuestActivationProvenance.h"
#include "Quests/Types/QuestEntryRecord.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "Quests/Types/QuestRuntimeRecord.h"
#include "QuestStateSubsystem.generated.h"

class AActor;
class USignalSubsystem;
class UWorldStateSubsystem;

/**
 * Multicast fired after any mutation to the registry maps (resolutions, entries, prereq cache). See
 * UQuestStateSubsystem::OnAnyRegistryChanged for semantics.
 */
DECLARE_MULTICAST_DELEGATE(FOnAnyRegistryChanged);

/**
 * Public read-side surface for quest state queries: past resolutions (rich-record half of the two-layer
 * state architecture; SimpleCore's UWorldStateSubsystem provides the boolean-fact half) and present-tense
 * activation queries (cached prereq snapshots, computed activation blockers).
 *
 * Naming convention mirrors UWorldStateSubsystem in SimpleCore: "State Subsystem" denotes a public,
 * externally-accessible fact registry with potentially limited write access. Designers come here for
 * quest-state queries, and the manager subsystem stays a black box for orchestration and pushes facts here
 * when state changes.
 *
 * Writes are exclusive to UQuestManagerSubsystem via friend access. External code never mutates this
 * subsystem. The manager pushes:
 *  - Known quest tag registration on graph activation (RegisterQuestTag) — populates the KnownQuests
 *     map whose keys answer GetQuestTagsUnderPrefix for hierarchical catch-up subscribers.
 *  - Resolution records on quest completion (RecordResolution).
 *  - Entry records on quest start (RecordEntry) — carries Provenance + ActivationParamsSnapshot +
 *     PathIdentity alongside the existing cascade fields, capturing the merged final params delivered
 *     to the objective so save/load can reconstitute live questline state by-value.
 *  - Prereq status snapshots on giver-branch entry and enablement-watch transitions (UpdateQuestPrereqStatus).
 *  - Cache clears on quest leaving giver state (ClearQuestPrereqStatus).
 *  - Container classification on graph activation (RegisterContainerTag).
 *
 * Reads are pure: blocker enumeration reads WorldState facts and the local CachedPrereqStatus map. No
 * manager dependency at query time.
 */
UCLASS()
class SIMPLEQUEST_API UQuestStateSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ── Past resolution queries ──────────────────────────────────────────────────────────────────────────

    /** Returns the full resolution record for a quest, or nullptr if the quest hasn't resolved this session. */
    const FQuestResolutionRecord* GetQuestResolution(FGameplayTag QuestTag) const;

    /** Convenience predicate: whether this quest has any resolution record this session. */
    bool HasResolved(FGameplayTag QuestTag) const;

    /**
     * Whether this quest has resolved with the specified OutcomeTag at any point this session. O(1) lookup against
     * a parallel index maintained alongside QuestResolutions; populated on every RecordResolution call. Works for
     * any OutcomeTag the quest has actually fired with, regardless of whether that outcome was registered at
     * compile time.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    bool HasResolvedWith(FGameplayTag QuestTag, FGameplayTag OutcomeTag) const;

    /** Convenience accessor: how many times this quest has resolved this session. */
    int32 GetResolutionCount(FGameplayTag QuestTag) const;

    /**
     * Returns the full chronological resolution history for a quest (every entry appended via RecordResolution).
     * Empty array if the quest hasn't resolved this session.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    TArray<FQuestResolutionEntry> GetResolutionHistory(FGameplayTag QuestTag) const;

    /** Returns the most recent resolution entry for a quest, or a default-constructed entry if no resolutions. */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    FQuestResolutionEntry GetLatestResolution(FGameplayTag QuestTag) const;

    // ── Past entry queries ───────────────────────────────────────────────────────────────────────────────

    /** Returns the full entry record for a quest, or nullptr if the quest hasn't been entered this session. */
    const FQuestEntryRecord* GetQuestEntry(FGameplayTag QuestTag) const;

    /** Convenience predicate: whether this quest has any entry record this session. */
    bool HasEntered(FGameplayTag QuestTag) const;

    /**
     * Whether this quest has been entered with the specified IncomingOutcomeTag at any point this session.
     * O(1) lookup against a parallel index maintained alongside QuestEntries. Used by Leaf_Entry prereqs
     * evaluating against this registry rather than against a WorldState fact.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    bool HasEnteredWith(FGameplayTag QuestTag, FGameplayTag IncomingOutcomeTag) const;

    /** Convenience accessor: how many times this quest has been entered this session. */
    int32 GetEntryCount(FGameplayTag QuestTag) const;

    /** Returns the full chronological entry history for a quest. Empty array if never entered this session. */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    TArray<FQuestEntryArrival> GetEntryHistory(FGameplayTag QuestTag) const;

    /** Returns the most recent entry for a quest, or a default-constructed entry if no entries. */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    FQuestEntryArrival GetLatestEntry(FGameplayTag QuestTag) const;


	// ── Quest registration + per-quest historical context ───────────────────────────────────────────────
	//
	// KnownQuests is the registry of quest tags the manager has registered this session via RegisterQuestTag
	// (called from RegisterQuestlineGraph). Keys answer "is this a known quest tag" and "what tags live under
	// this prefix" — the hierarchical catch-up entry point. Values hold quest-level historical context that
	// isn't tied to an individual start arrival (per-start detail lives on FQuestEntryArrival).

	/**
	 * Returns every known quest tag that matches Prefix or is a descendant of Prefix. Used by hierarchical
	 * catch-up subscribers to enumerate descendants of a parent-prefix subscription. Empty result for an
	 * invalid Prefix or when no descendants are known.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|State")
	TArray<FGameplayTag> GetQuestTagsUnderPrefix(FGameplayTag Prefix) const;

	/**
	 * True if ContextualTag has been registered with the manager this session via RegisterQuestTag. Distinct from
	 * FQuestTagComposer::IsTagRegisteredInRuntime, which checks compile-time gameplay tag registration; this
	 * predicate answers "has the runtime instance been wired up."
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	bool IsKnownQuestTag(FGameplayTag QuestTag) const;

	/** Number of known quest tags this session. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	int32 GetKnownQuestTagCount() const;

	/** Returns the runtime record for ContextualTag, or nullptr if the tag isn't a known quest tag. */
	const FQuestRuntimeRecord* GetQuestRuntimeRecord(FGameplayTag QuestTag) const;

	/**
	 * The actor that initiated the most-recent start of this quest (UQuestStep::ReceivedActivationParams.ActivationSource
	 * captured at start time, preserved past the live step's deactivation). Null for non-Step starts (containers
	 * have no objective; no params snapshot) and for starts where no ActivationSource was supplied.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	AActor* GetLastGiverActor(FGameplayTag QuestTag) const;

	/**
	 * Provenance of the most-recent start of this quest. EQuestActivationProvenance::Unknown if the quest hasn't started
	 * this session or pre-dates Provenance stamping.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	EQuestActivationProvenance GetLastActivationProvenance(FGameplayTag QuestTag) const;

	/**
	 * By-value snapshot of the merged final FQuestObjectiveActivationParams delivered to the objective at the most-
	 * recent start (UQuestStep::ReceivedActivationParams). Default-constructed for non-Step starts and for quests that
	 * haven't started this session. Sufficient to reconstitute the live questline's objective state for save/load.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	FQuestObjectiveActivationParams GetLastActivationParamsSnapshot(FGameplayTag QuestTag) const;

	/**
	 * Per-source routing identity from the most-recent start. NAME_None for entry-tag fires and any start that didn't
	 * carry per-source routing.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	FName GetLastPathIdentity(FGameplayTag QuestTag) const;
	

    // ── Read-side enumeration (inspection-only) ──────────────────────────────────────────────────────────
    //
    // Exposes the underlying registry maps as const refs so editor / debug surfaces (Quest State facts panel,
    // future telemetry tools) can walk the full registry without per-quest probes. Mutation stays friend-
    // protected through UQuestManagerSubsystem; these accessors are read-only by const-ness.

    /** All resolved quests this session, keyed by quest tag. Values are append-only history records. */
    const TMap<FGameplayTag, FQuestResolutionRecord>& GetAllResolutions() const { return QuestResolutions; }

    /** All entered destination quests this session, keyed by destination quest tag. */
    const TMap<FGameplayTag, FQuestEntryRecord>& GetAllEntries() const { return QuestEntries; }

	/**
	 * All known quest tags this session, keyed by quest tag. The map's keys are the canonical answer to
	 * "what tags has the manager registered" — used by the hierarchical catch-up walk. Values hold per-quest
	 * historical context (RegisteredTime; future quest-level fields).
	 */
	const TMap<FGameplayTag, FQuestRuntimeRecord>& GetAllKnownQuests() const { return KnownQuests; }
	
    /** All quests currently in PendingGiver state with a cached prereq snapshot. Cleared on giver-state exit. */
    const TMap<FGameplayTag, FQuestPrereqStatus>& GetAllCachedPrereqStatus() const { return CachedPrereqStatus; }

    /**
     * Multicast fired after any mutation to the registry maps — RecordResolution, RecordEntry, UpdateQuest-
     * PrereqStatus, ClearQuestPrereqStatus. Distinct from the per-quest FQuestResolutionRecordedEvent / FQuest-
     * EntryRecordedEvent publishes used by prereq-leaf subscribers. This is a "registry mutated, refresh if
     * you care about the whole map" signal for inspection surfaces (Quest State Facts Panel, future telemetry
     * tools). Fires synchronously inside the mutation method, after the per-quest publish (if any).
     */
    FOnAnyRegistryChanged OnAnyRegistryChanged;


    // ── Present-tense activation queries ─────────────────────────────────────────────────────────────────

    /**
     * Returns the current set of activation blockers for ContextualTag — empty array means the quest is currently
     * startable. State-fact blockers (UnknownQuest, AlreadyLive, Blocked, Deactivated, NotPendingGiver) come
     * first; PrereqUnmet comes last with UnsatisfiedLeafTags populated. Computed from WorldState facts +
     * cached prereq status. Pure read; no manager interaction.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    TArray<FQuestActivationBlocker> QueryQuestActivationBlockers(FGameplayTag QuestTag) const;

    /**
     * Returns the cached prereq status for a quest in PendingGiver state. For quests not in PendingGiver
     * state (or with no cached entry), returns a default-constructed status (bIsAlways=true, bSatisfied=true).
     * Caller should branch on QueryQuestActivationBlockers' UnknownQuest / NotPendingGiver blockers to
     * disambiguate "no prereqs to worry about" from "quest isn't currently in giver state."
     */
    UFUNCTION(BlueprintCallable, Category = "Quest|State")
    FQuestPrereqStatus GetQuestPrereqStatus(FGameplayTag QuestTag) const;

	/**
	 * Whether ContextualTag's runtime instance is a UQuest container (wrapper). False for Steps, utility nodes, and
	 * any tag the manager hasn't registered. Public read surface — used by the blocker query and any consumer
	 * that needs to know a tag's structural classification.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|State")
	bool IsContainerTag(FGameplayTag QuestTag) const;

private:
    friend class UQuestManagerSubsystem;

    UPROPERTY()
    TMap<FGameplayTag, FQuestResolutionRecord> QuestResolutions;

    /**
     * Parallel O(1) index for HasResolvedWith. Maintained alongside QuestResolutions: every RecordResolution call
     * adds the (ContextualTag, OutcomeTag) pair to this map. Avoids walking History for outcome-keyed prereq queries.
     */
    TMap<FGameplayTag, TSet<FGameplayTag>> ResolvedOutcomesByQuest;

    UPROPERTY()
    TMap<FGameplayTag, FQuestEntryRecord> QuestEntries;

    /**
     * Parallel O(1) index for HasEnteredWith. Maintained alongside QuestEntries: every RecordEntry call
     * adds the (ContextualTag, IncomingOutcomeTag) pair. TSet handles deduplication so repeat entries with the
     * same outcome don't bloat the set.
     */
    TMap<FGameplayTag, TSet<FGameplayTag>> EnteredOutcomesByQuest;
    
    /** Cache of current prereq status per quest in PendingGiver state. Populated by the manager's giver branch
     *  and updated on enablement-watch transitions. Cleared when the quest leaves giver state. */
    TMap<FGameplayTag, FQuestPrereqStatus> CachedPrereqStatus;

    /** Manager calls these via friend access. */
    void RecordResolution(FGameplayTag QuestTag, FGameplayTag OutcomeTag, double ResolutionTime, EQuestResolutionSource Source);
    void UpdateQuestPrereqStatus(FGameplayTag QuestTag, const FQuestPrereqStatus& Status);
    void ClearQuestPrereqStatus(FGameplayTag QuestTag);
    void RecordEntry(
    	FGameplayTag QuestTag,
    	FGameplayTag SourceQuestTag,
    	FGameplayTag IncomingOutcomeTag,
    	double EntryTime,
    	EQuestActivationProvenance Provenance,
    	const FQuestObjectiveActivationParams& ActivationParamsSnapshot,
    	FName PathIdentity);

	/**
	 * Registers ContextualTag into KnownQuests with a default-constructed FQuestRuntimeRecord stamped with current world time.
	 * Idempotent — repeat calls on the same tag preserve the earliest RegisteredTime. Called from
	 * UQuestManagerSubsystem::RegisterQuestlineGraph for every valid resolved tag in the graph's compiled nodes.
	 */
	void RegisterQuestTag(FGameplayTag QuestTag);
	
    /** Resolves the GameInstance's WorldState subsystem for the blocker-fact lookups. */
    UWorldStateSubsystem* ResolveWorldState() const;
    
    /** Resolves the GameInstance's SignalSubsystem for publishing FQuestResolutionRecordedEvent on RecordResolution. */
    USignalSubsystem* ResolveSignalSubsystem() const;

	/**
	 * Pushed by the manager during graph activation: marks ContextualTag as a container (UQuest wrapper). Lets the
	 * blocker query distinguish Step-vs-container semantics for the AlreadyLive blocker without cross-subsystem
	 * coupling — containers' Live state is derived from inner Step state and shouldn't gate forward activation.
	 */
	void RegisterContainerTag(FGameplayTag QuestTag);

	/**
	 * Set of compiled QuestTags whose runtime instance is a UQuest container. Populated by the manager during
	 * ActivateQuestlineGraph; read by QueryQuestActivationBlockers. Persists with the subsystem instance.
	 */
	TSet<FGameplayTag> ContainerTags;
	
	/** All quest tags registered this session, mapped to their per-quest runtime record. The key set answers
	 *  GetQuestTagsUnderPrefix for hierarchical catch-up; the value set holds quest-level historical context
	 *  (RegisteredTime today; future quest-level fields land here without restructuring). Manager pushes via
	 *  RegisterQuestTag during RegisterQuestlineGraph. */
	UPROPERTY()
	TMap<FGameplayTag, FQuestRuntimeRecord> KnownQuests;
};