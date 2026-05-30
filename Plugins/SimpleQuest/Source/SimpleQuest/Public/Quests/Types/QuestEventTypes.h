// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestEventPayload.h"
#include "QuestEventTypes.generated.h"

class AActor;

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
    /** Configuration-only flag meaning "no event chosen". Lifecycle events arriving at runtime are always one of the non-None values below. */
    None,
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

/**
 * Aggregate payload for the catch-all OnAnyQuestEvent delegate on UQuestObserverComponent. Carries the common
 * fields a broad-audience consumer needs (UI sidebars, audio routers, telemetry pipelines, etc.) without
 * requiring the consumer to bind every per-type delegate just to discriminate on EventType.
 *
 * Field population by EventType:
 *   - QuestTag, MatchedChannel, EventType: always populated
 *   - OutcomeTag: populated on Completed; default (empty) otherwise
 *   - GiverActor: populated on Started for giver-gated quests; nullptr otherwise
 *
 * Consumers needing richer event-specific payloads (Progress amounts, Block reasons, GiveBlocked blocker
 * arrays, etc.) bind the corresponding narrow delegate instead — those payloads stay on the per-type
 * delegates rather than bloating the catch-all into a union.
 */
USTRUCT(BlueprintType)
struct FQuestLifecycleEventReport
{
    GENERATED_BODY()

    /** Canonical quest tag for the event source (the publishing instance's ContextualTag / Stack[0]). */
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag QuestTag;

    /** Delivery metadata — the channel from the publishing set most specific to this observer's bound tag. */
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag MatchedChannel;

    /** Which lifecycle event arrived. Never None at dispatch — None is reserved for configuration sentinels. */
    UPROPERTY(BlueprintReadOnly)
    EQuestLifecycleEventType EventType = EQuestLifecycleEventType::None;

    /**
     * Per-event payload — NodeInfo, CompletionTrigger, plus inherited FQuestContextBase fields (Instigator,
     * CustomData, OriginTag, OriginChain). Common across all lifecycle events; carries the same data the
     * narrow delegates' Payload parameter does. On catch-up, only NodeInfo.QuestTag is populated — full
     * payload isn't recoverable from state alone (matches the narrow delegates' catch-up contract).
     */
    UPROPERTY(BlueprintReadOnly)
    FQuestEventPayload Payload;
    
    /** Populated on Completed; default (empty) tag otherwise. */
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag OutcomeTag;

    /** Populated on Started or Give Blocked for giver-gated quests; nullptr otherwise. */
    UPROPERTY(BlueprintReadOnly)
    AActor* GiverActor = nullptr;
};