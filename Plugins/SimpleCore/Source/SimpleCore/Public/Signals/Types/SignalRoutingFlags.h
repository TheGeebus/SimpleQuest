// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "SignalRoutingFlags.generated.h"

/**
 * Shared 4-way routing vocabulary for the SimpleCore signal bus. Used on both subscribe and publish sides. 
 * The flag names describe positions in the tag hierarchy relative to the named tag; semantics mirror naturally
 * across sides.
 *
 * - Subscribe-side reading: which publish channels do I want events from?
 *  - Ancestors   — published events on my ancestors
 *  - Descendants — published events on my descendants 
 *
 * - Publish-side reading (future): which subscribers do I want to reach?
 *  - Ancestors   — additional subscribers above the published channel (walk ancestor tags)
 *  - Descendants — additional subscribers below the published channel (walk descendant trees)
 *
 * Default for subscribe is ExactMatch | Descendants — preserves the bus's current hierarchical behavior.
 * Subscribers narrow to ExactMatch when ancestor-walk delivery would be noise (e.g., a Giver that cares
 * only about its specific quest tag, not the quest's inner Step publishes).
 */
UENUM(Flags, BlueprintType)
enum class ESignalRoutingFlags : uint8
{
    ExactMatchOnly          = 0       UMETA(Hidden, DisplayName = "Exact Match", ToolTip = "Exact-match only — receive/deliver to the named tag itself."),
    Ancestors               = 1 << 0  UMETA(DisplayName = "Ancestors",   ToolTip = "Extend scope upward — also include tags above the named tag."),
    Descendants             = 1 << 1  UMETA(DisplayName = "Descendants", ToolTip = "Extend scope downward — also include tags below the named tag."),
};
ENUM_CLASS_FLAGS(ESignalRoutingFlags);

namespace SignalRoutingDefaults
{
    /** No scope extensions — receive/deliver only on the exact named channel. */
    inline constexpr ESignalRoutingFlags ExactOnly = ESignalRoutingFlags::ExactMatchOnly;

    /**
     * Subscribe-side hierarchical default — receive on the exact channel OR any descendant (the bus's
     * current ancestor-walk behavior). Preserves prior behavior for all existing call sites.
     */
    inline constexpr ESignalRoutingFlags HierarchicalSubscribe = ESignalRoutingFlags::Descendants;

    /**
     * Publish-side hierarchical default — reserved for the publish-side bitmask design. Deliver on the
     * exact channel AND every ancestor (the bus's current ancestor-walk-on-publish behavior).
     */
    inline constexpr ESignalRoutingFlags HierarchicalPublish = ESignalRoutingFlags::Ancestors;

    /** Both directions — full vertical chain through the named tag. */
    inline constexpr ESignalRoutingFlags Bidirectional = static_cast<ESignalRoutingFlags>(
        static_cast<uint8>(ESignalRoutingFlags::Ancestors) | static_cast<uint8>(ESignalRoutingFlags::Descendants));
}