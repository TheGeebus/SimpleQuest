// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "QuestResolutionSubsystem.generated.h"

/**
 * Public query layer for quest resolution data — the rich-record half of the two-layer state architecture
 * (SimpleCore's UWorldStateSubsystem provides the boolean-fact half). Consumers ask this subsystem "what
 * outcome did quest X resolve with?" via GetQuestResolution; for the boolean "has it resolved at all?"
 * question, check WorldState for the QuestState.<X>.Completed fact (O(1) vs this subsystem's O(1) map
 * lookup — both are cheap).
 *
 * Writes are exclusive to UQuestManagerSubsystem via friend access — external code never mutates this
 * subsystem. That preserves the manager's "black box" design contract: the manager is the sole owner
 * of quest orchestration; consumers read from specialized data-query subsystems like this one.
 *
 * Lifetime is GameInstance-scoped, so records reset naturally on PIE transitions. This matches
 * UQuestManagerSubsystem's lifetime and WorldState's lifetime, keeping all three layers in sync.
 */
UCLASS()
class SIMPLEQUEST_API UQuestResolutionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /**
     * Returns the full resolution record for a quest, or nullptr if the quest hasn't resolved yet this session. Use the record's
     * OutcomeTag to recover which outcome a quest resolved with when subscribing after the fact (catch-up).
     */
    const FQuestResolutionRecord* GetQuestResolution(FGameplayTag QuestTag) const;

    /** Convenience predicate — whether this quest has any resolution record this session. */
    bool HasResolved(FGameplayTag QuestTag) const;

    /**
     * Convenience accessor — how many times this quest has resolved this session. Replaces the previous UQuestManagerSubsystem-owned
     * QuestCompletionCounts map.
     */
    int32 GetResolutionCount(FGameplayTag QuestTag) const;

private:
    /**
     * Only UQuestManagerSubsystem writes to the registry. Keeping this private + friend enforces the "manager owns orchestration,
     * registry answers post-mortem queries" separation at the type system level — consumers physically can't mutate.
     */
    friend class UQuestManagerSubsystem;

    UPROPERTY()
    TMap<FGameplayTag, FQuestResolutionRecord> QuestResolutions;

    /**
     * Manager calls this from SetQuestResolved, paired with its own WorldState-fact writes. Bumps ResolutionCount on repeat resolutions
     * of the same quest.
     */
    void RecordResolution(FGameplayTag QuestTag, FGameplayTag OutcomeTag, double ResolutionTime);
};