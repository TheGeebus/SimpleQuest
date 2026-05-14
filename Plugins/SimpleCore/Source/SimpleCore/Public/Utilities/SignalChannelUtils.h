// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"


/**
 * Pure tag-channel selection helpers used by the multi-channel publish flow and any consumer that needs to
 * mirror its delivery semantics in a synthetic broadcast (catch-up dispatchers, replay tools, debug overlays).
 * No bus state, no subsystem dependencies — header-only inline functions, testable in isolation.
 *
 * Naming convention follows SimpleQuest's domain-specific utility namespaces (FQuestPublish, FQuestTagComposer,
 * FQuestCatchUpFanout) — FSignalChannelUtils is the SimpleCore-side equivalent for signal-channel mechanics.
 */
namespace FSignalChannelUtils
{
    /**
     * Picks the longest channel from Channels where BoundTag is an ancestor (or equals BoundTag itself). Tie-break
     * by input array order — first occurrence wins. Returns invalid tag when no channel in the set has BoundTag in
     * its ancestor chain (the subscriber wouldn't legitimately receive any of these channels via the bus's
     * hierarchical-walk semantic).
     *
     * Used by the bus's multi-channel publish dispatcher to set the per-subscriber matched-channel arg, and by
     * synthetic-broadcast paths (catch-up dispatchers) to compute the same matched-channel value so subscribers
     * see consistent values across delivery paths.
     */
    inline FGameplayTag PickBestMatchChannel(const TArray<FGameplayTag>& Channels, const FGameplayTag& BoundTag)
    {
        FGameplayTag Best;
        int32 BestDepth = -1;
        for (const FGameplayTag& C : Channels)
        {
            // C.MatchesTag(BoundTag) is true when C equals BoundTag or is a descendant — i.e., when BoundTag
            // appears on C's ancestor walk to root. That's the relation a subscriber bound at BoundTag uses to
            // legitimately receive events published on C.
            if (!C.MatchesTag(BoundTag)) continue;

            int32 Depth = 0;
            FGameplayTag Walker = C;
            while (Walker.IsValid())
            {
                ++Depth;
                Walker = Walker.RequestDirectParent();
            }
            if (Depth > BestDepth)
            {
                Best = C;
                BestDepth = Depth;
            }
        }
        return Best;
    }
}