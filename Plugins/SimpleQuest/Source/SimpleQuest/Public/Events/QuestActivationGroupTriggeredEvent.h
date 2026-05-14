// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/OriginatingEventID.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "QuestActivationGroupTriggeredEvent.generated.h"


/**
 * Transient signal published by UActivationGroupSetterNode when its Activate input fires. The signal channel is the group's
 * gameplay tag itself — every UActivationGroupListenerNode subscribed to that group tag receives the signal regardless of
 * whether its containing wrapper is currently Live (Listeners subscribe at instance lifetime via OnRegisteredWithManager,
 * not at wrapper Activate, so the always-armed semantic holds across loop iterations and cross-graph boundaries).
 *
 * No WorldState involvement. Replaces the prior persistent-fact + Listener-at-entry-routes mechanism whose iteration-N
 * loop case short-circuited on the stale fact.
 *
 * Group is transparent to chain bookkeeping: Listener stamps OriginChain straight onto each NextNodesOnForward destination's
 * PendingActivationContext without appending its own tag. SourceTag is preserved as signal provenance for diagnostic logging
 * and designer-facing introspection only — not as a chain extension.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestActivationGroupTriggeredEvent
{
    GENERATED_BODY()

    FQuestActivationGroupTriggeredEvent() = default;

    FQuestActivationGroupTriggeredEvent(FGameplayTag InGroupTag, const FQuestObjectiveActivationContext& InForwardParams,
        FName InSourceTag, const TArray<FGameplayTag>& InOriginChain,
        const FOriginatingEventID& InOriginatingEventID = FOriginatingEventID())
        : GroupTag(InGroupTag), ForwardParams(InForwardParams), SourceTag(InSourceTag), OriginChain(InOriginChain),
          OriginatingEventID(InOriginatingEventID) {}

    /** The group's gameplay tag — also the signal channel. */
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag GroupTag;

    /** Activation params reaching the Setter. Listener stamps these onto each NextNodesOnForward destination's PendingActivationContext. */
    UPROPERTY(BlueprintReadOnly)
    FQuestObjectiveActivationContext ForwardParams;

    /** Compiled ContextualTag of the upstream source whose outcome activated the Setter. Diagnostic / signal-provenance only — not chain-extended. */
    UPROPERTY(BlueprintReadOnly)
    FName SourceTag = NAME_None;

    /** Activation chain reaching the Setter, threaded transparently through the group (Listener doesn't append its own tag). */
    UPROPERTY(BlueprintReadOnly)
    TArray<FGameplayTag> OriginChain;

    /**
     * Cascade event ID threaded transparently through the group (Listener doesn't mutate; just propagates onto
     * destinations). Mirrors the OriginChain duplication pattern: lives both inside ForwardParams AND as a first-
     * class event field for designer BP introspection — designers reading the event payload see "what triggered
     * this group fire?" without reaching into ForwardParams.
     */
    UPROPERTY(BlueprintReadOnly)
    FOriginatingEventID OriginatingEventID;
};