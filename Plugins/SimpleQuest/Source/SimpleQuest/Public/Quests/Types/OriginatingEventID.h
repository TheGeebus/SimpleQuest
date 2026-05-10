// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OriginatingEventID.generated.h"


/**
 * Multi-tag-stable identity for the gameplay event that originated a cascade. Composed of two pieces:
 *
 *   AuthoredNodeGuid — the originating node's authored identity (UQuestlineNodeBase::QuestGuid before the
 *     placement-chain combination that produces QuestContentGuid). Same authored Step in two compile contexts
 *     (e.g., Main inlined + NewTest standalone) shares this value, so cascades from "the same logical event
 *     reaching me through different per-perspective paths" can be recognized as one.
 *
 *   ResolutionTimestamp — GetWorld()->GetTimeSeconds() at the originating SetQuestResolved call. Per-tick stable
 *     (constant within one tick), distinct across ticks. Distinguishes a re-firing of the same Step at a later
 *     moment (loop iteration or stays-Live re-resolution) from the multi-tag fanout of a single firing.
 *
 * Used by the wrapper completion gate: each container wrapper holds a per-instance set of event IDs that have
 * already resolved it in the current Live phase. A second resolution attempt with a matching ID is recognized as
 * the multi-tag fanout of an already-processed event and skipped; a different ID (later tick / different
 * originating Step) proceeds normally.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FOriginatingEventID
{
    GENERATED_BODY()

    /** Authored identity of the originating node — UQuestlineNodeBase::QuestGuid before placement-chain combination. */
    UPROPERTY(BlueprintReadOnly)
    FGuid AuthoredNodeGuid;

    /** World seconds at the originating SetQuestResolved call. Per-tick stable; distinguishes re-firings across ticks. */
    UPROPERTY(BlueprintReadOnly)
    double ResolutionTimestamp = 0.0;

    bool operator==(const FOriginatingEventID& Other) const
    {
        return AuthoredNodeGuid == Other.AuthoredNodeGuid && ResolutionTimestamp == Other.ResolutionTimestamp;
    }

    friend uint32 GetTypeHash(const FOriginatingEventID& Key)
    {
        return HashCombine(GetTypeHash(Key.AuthoredNodeGuid), GetTypeHash(Key.ResolutionTimestamp));
    }

    /**
     * True when both fields are populated. False for default-constructed instances (e.g., direct external
     * resolution that doesn't run through a cascade — wrapper gate skips dedup logic in that case).
     */
    bool IsValid() const { return AuthoredNodeGuid.IsValid(); }
};