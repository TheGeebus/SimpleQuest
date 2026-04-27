// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/Types/QuestActivationBlocker.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "QuestStateSubsystem.generated.h"

class UWorldStateSubsystem;

/**
 * Public read-side surface for quest state queries — past resolutions (rich-record half of the two-layer
 * state architecture; SimpleCore's UWorldStateSubsystem provides the boolean-fact half) and present-tense
 * activation queries (cached prereq snapshots, computed activation blockers).
 *
 * Naming convention mirrors UWorldStateSubsystem in SimpleCore — "State Subsystem" denotes a public,
 * externally-accessible fact registry with potentially limited write access. Designers come here for
 * quest-state queries; the manager subsystem stays a black box for orchestration and pushes facts here
 * when state changes.
 *
 * Writes are exclusive to UQuestManagerSubsystem via friend access — external code never mutates this
 * subsystem. The manager pushes:
 *   - Resolution records on quest completion (RecordResolution).
 *   - Prereq status snapshots on giver-branch entry and enablement-watch transitions (UpdateQuestPrereqStatus).
 *   - Cache clears on quest leaving giver state (ClearQuestPrereqStatus).
 *
 * Reads are pure — blocker enumeration reads WorldState facts + the local CachedPrereqStatus map. No
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

    /** Convenience predicate — whether this quest has any resolution record this session. */
    bool HasResolved(FGameplayTag QuestTag) const;

    /** Convenience accessor — how many times this quest has resolved this session. */
    int32 GetResolutionCount(FGameplayTag QuestTag) const;

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

    /** Cache of current prereq status per quest in PendingGiver state. Populated by the manager's giver branch
     *  and updated on enablement-watch transitions. Cleared when the quest leaves giver state. */
    TMap<FGameplayTag, FQuestPrereqStatus> CachedPrereqStatus;

    /** Manager calls these via friend access. */
    void RecordResolution(FGameplayTag QuestTag, FGameplayTag OutcomeTag, double ResolutionTime);
    void UpdateQuestPrereqStatus(FGameplayTag QuestTag, const FQuestPrereqStatus& Status);
    void ClearQuestPrereqStatus(FGameplayTag QuestTag);

    /** Resolves the GameInstance's WorldState subsystem for the blocker-fact lookups. */
    UWorldStateSubsystem* ResolveWorldState() const;
};