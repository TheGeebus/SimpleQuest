// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestEventTypes.generated.h"

/**
 * Quest lifecycle event-type enums — two shapes for two roles.
 *
 * EQuestEventTypes (bitmask, uint16): multi-select EXPOSURE configuration. "Which
 * events do I want to subscribe to?" Used on the ObserveQuestLifecycle K2 node's
 * per-flag Details-panel checkboxes, on FObservedQuestEventSettings's per-event
 * bools, and on the proxy's ExposedEventsMask int32. The 10 flags (bits 0-9)
 * require uint16 storage, which precludes BlueprintType — adopters needing a
 * BP-typed event identifier reach for the companion enum below.
 *
 * EQuestLifecycleEventType (single-value, uint8): single-value ARRIVAL identification.
 * "Which event arrived?" BlueprintType for use as struct fields, BP function
 * parameters, and BP Switch operands. Mirrors EQuestEventTypes's named flags
 * one-for-one (minus the bitmask semantics).
 *
 * The two are intentionally kept distinct rather than collapsed because their
 * shapes serve incompatible roles — bitmask wide enough for 10 flags vs single-
 * value narrow enough for BP-typed reflection. ToEventTypeMask below bridges them
 * for adopter C++ that needs to test arrival identity against exposure config.
 */

/**
 * Bitflag mask of which lifecycle events a subscriber has exposed. The proxy gates
 * SubscribeMessage calls and catch-up branches by this mask, so unexposed events
 * incur zero subscription cost.
 */
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EQuestEventTypes : uint16
{
    None        = 0       UMETA(Hidden),
    Activated   = 1 << 0,
    Enabled     = 1 << 1,
    Disabled    = 1 << 2,
    GiveBlocked = 1 << 3,
    Started     = 1 << 4,
    Progress    = 1 << 5,
    Completed   = 1 << 6,
    Deactivated = 1 << 7,
    Blocked     = 1 << 8,
    Unblocked   = 1 << 9,
};
ENUM_CLASS_FLAGS(EQuestEventTypes);

/**
 * Single-value identifier for which lifecycle event was delivered. Companion to
 * the EQuestEventTypes bitmask above; same conceptual space, distinct roles
 * (single-value arrival vs multi-select exposure).
 *
 * uint8-backed BlueprintType for use as struct fields, BP function parameters,
 * BP Switch operands. Mirrors EQuestEventTypes's named flags one-for-one.
 */
UENUM(BlueprintType)
enum class EQuestLifecycleEventType : uint8
{
    None = 0        UMETA(Hidden),
    Activated,
    Enabled,
    Disabled,
    GiveBlocked,
    Started,
    Progress,
    Completed,
    Deactivated,
    Blocked,
    Unblocked,
};

/** Convert a single-value lifecycle identifier to its bitmask equivalent. */
FORCEINLINE EQuestEventTypes ToEventTypeMask(EQuestLifecycleEventType Single)
{
    switch (Single)
    {
        case EQuestLifecycleEventType::Activated:   return EQuestEventTypes::Activated;
        case EQuestLifecycleEventType::Enabled:     return EQuestEventTypes::Enabled;
        case EQuestLifecycleEventType::Disabled:    return EQuestEventTypes::Disabled;
        case EQuestLifecycleEventType::GiveBlocked: return EQuestEventTypes::GiveBlocked;
        case EQuestLifecycleEventType::Started:     return EQuestEventTypes::Started;
        case EQuestLifecycleEventType::Progress:    return EQuestEventTypes::Progress;
        case EQuestLifecycleEventType::Completed:   return EQuestEventTypes::Completed;
        case EQuestLifecycleEventType::Deactivated: return EQuestEventTypes::Deactivated;
        case EQuestLifecycleEventType::Blocked:     return EQuestEventTypes::Blocked;
        case EQuestLifecycleEventType::Unblocked:   return EQuestEventTypes::Unblocked;
        default:                                    return EQuestEventTypes::None;
    }
}