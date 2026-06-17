// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "SignalRoutingFlags.generated.h"

/**
 * Routing mode for signal-bus subscribe / publish calls. Picks which directional walks the call participates
 * in relative to the bound tag.
 *
 * - ExactMatch — receive/deliver only on the named tag itself, no walks.
 * - Ancestors — additionally walk upward to subscribers/publishes on ancestor channels (publish-side descendant-
 *   walk; reserved for future — currently a no-op on subscribe side, has no publish-side dispatch yet).
 * - Descendants — additionally walk downward to events published on descendant channels (subscribe-side ancestor-
 *   walk; the bus's existing hierarchical delivery behavior).
 * - Bidirectional — both Ancestors and Descendants walks active.
 */
UENUM(BlueprintType)
enum class ESignalRoutingMode : uint8
{
    ExactMatch     UMETA(DisplayName = "Exact Match",    ToolTip = "Receive/deliver only on the exact named tag. No ancestor or descendant walks."),
    Ancestors      UMETA(DisplayName = "Ancestors",      ToolTip = "Also include events on ancestor channels. Reserved for future — publish-side descendant-walk not yet implemented."),
    Descendants    UMETA(DisplayName = "Descendants",    ToolTip = "Also include events published on descendant channels. The bus's existing hierarchical delivery default."),
    Bidirectional  UMETA(DisplayName = "Bidirectional",  ToolTip = "Both Ancestors and Descendants walks active."),
};

namespace FSignalRoutingDefaults
{
    /** Subscribe-side hierarchical default — receive on the exact channel OR any descendant. Preserves the
     *  bus's prior delivery behavior for all existing call sites. */
    inline constexpr ESignalRoutingMode HierarchicalSubscribe = ESignalRoutingMode::Descendants;

    /** Convenience alias for "no scope extensions" callers — receive/deliver only on the exact named channel. */
    inline constexpr ESignalRoutingMode ExactOnly = ESignalRoutingMode::ExactMatch;

    /** True when the given mode opts into receiving events from descendant channels (Descendants or Bidirectional).
     *  Replaces the prior EnumHasAnyFlags(Routing, ::Descendants) pattern at dispatch sites. */
    inline constexpr bool IncludesDescendants(ESignalRoutingMode Mode)
    {
        return Mode == ESignalRoutingMode::Descendants || Mode == ESignalRoutingMode::Bidirectional;
    }

    /** True when the given mode opts into receiving events from ancestor channels (Ancestors or Bidirectional).
     *  Reserved for future publish-side descendant-walk dispatch. */
    inline constexpr bool IncludesAncestors(ESignalRoutingMode Mode)
    {
        return Mode == ESignalRoutingMode::Ancestors || Mode == ESignalRoutingMode::Bidirectional;
    }
}