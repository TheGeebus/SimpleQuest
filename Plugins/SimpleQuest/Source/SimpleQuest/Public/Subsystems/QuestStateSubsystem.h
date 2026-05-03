// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "Quests/Types/QuestEntryRecord.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "QuestStateSubsystem.generated.h"

class USignalSubsystem;
class UWorldStateSubsystem;

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
 *   - Resolution records on quest completion (RecordResolution).
 *   - Prereq status snapshots on giver-branch entry and enablement-watch transitions (UpdateQuestPrereqStatus).
 *   - Cache clears on quest leaving giver state (ClearQuestPrereqStatus).
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

    /** Returns the full chronological resolution history for a quest (every entry appended via RecordResolution).
     * Empty array if the quest hasn't resolved this session. */
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


    // ── Read-side enumeration (inspection-only) ──────────────────────────────────────────────────────────
    //
    // Exposes the underlying registry maps as const refs so editor / debug surfaces (Quest State facts panel,
    // future telemetry tools) can walk the full registry without per-quest probes. Mutation stays friend-
    // protected through UQuestManagerSubsystem; these accessors are read-only by const-ness.

    /** All resolved quests this session, keyed by quest tag. Values are append-only history records. */
    const TMap<FGameplayTag, FQuestResolutionRecord>& GetAllResolutions() const { return QuestResolutions; }

    /** All entered destination quests this session, keyed by destination quest tag. */
    const TMap<FGameplayTag, FQuestEntryRecord>& GetAllEntries() const { return QuestEntries; }

    /** All quests currently in PendingGiver state with a cached prereq snapshot. Cleared on giver-state exit. */
    const TMap<FGameplayTag, FQuestPrereqStatus>& GetAllCachedPrereqStatus() const { return CachedPrereqStatus; }


    // ── Present-tense activation queries ─────────────────────────────────────────────────────────────────

    /**
     * Returns the current set of activation blockers for QuestTag — empty array means the quest is currently
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

private:
    friend class UQuestManagerSubsystem;

    UPROPERTY()
    TMap<FGameplayTag, FQuestResolutionRecord> QuestResolutions;

    /**
     * Parallel O(1) index for HasResolvedWith. Maintained alongside QuestResolutions: every RecordResolution call
     * adds the (QuestTag, OutcomeTag) pair to this map. Avoids walking History for outcome-keyed prereq queries.
     */
    TMap<FGameplayTag, TSet<FGameplayTag>> ResolvedOutcomesByQuest;

    UPROPERTY()
    TMap<FGameplayTag, FQuestEntryRecord> QuestEntries;

    /**
     * Parallel O(1) index for HasEnteredWith. Maintained alongside QuestEntries: every RecordEntry call
     * adds the (QuestTag, IncomingOutcomeTag) pair. TSet handles deduplication so repeat entries with the
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
    void RecordEntry(FGameplayTag QuestTag, FGameplayTag SourceQuestTag, FGameplayTag IncomingOutcomeTag, double EntryTime);

    /** Resolves the GameInstance's WorldState subsystem for the blocker-fact lookups. */
    UWorldStateSubsystem* ResolveWorldState() const;
    
    /** Resolves the GameInstance's SignalSubsystem for publishing FQuestResolutionRecordedEvent on RecordResolution. */
    USignalSubsystem* ResolveSignalSubsystem() const;
};