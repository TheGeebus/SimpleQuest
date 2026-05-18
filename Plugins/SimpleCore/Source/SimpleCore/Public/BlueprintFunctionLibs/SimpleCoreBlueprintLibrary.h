// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StructUtils/InstancedStruct.h"
#include "WorldState/WorldStateSubsystem.h"  // for EFactBroadcastMode (UENUM in default arg)
#include "SimpleCoreBlueprintLibrary.generated.h"

class USignalSubsystem;


/**
 * Blueprint-facing entry points for SimpleCore. Mirrors USimpleQuestBlueprintLibrary's pattern: static
 * function-library methods that resolve the underlying subsystem from a WorldContext object and forward
 * the call. Designers + adopter BP code reach SimpleCore exclusively through this surface; the subsystems
 * themselves stay BP-private.
 *
 * Why this layer exists: USignalSubsystem's templated SubscribeMessage / PublishMessage methods can't be
 * UFUNCTION-tagged (templates aren't reflectable), and exposing the raw subsystems to BP would force
 * adopters to learn the GetWorld → GameInstance → Subsystem resolution dance per call. The library hides
 * that resolution behind named entry points and provides FInstancedStruct-payload BP wrappers for the
 * publish APIs. Aligns with principle #11 (orchestrator subsystems stay BP-private; public read / write
 * surface lives on a clearly-named consumer-facing layer).
 */
UCLASS()
class SIMPLECORE_API USimpleCoreBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── Signals ────────────────────────────────────────────────────────────────────────────────────

    /**
     * Publish an event payload on a single tag channel. The bus walks Channel's hierarchy and delivers to
     * subscribers at the channel itself or any ancestor. Payload is any USTRUCT, packed into an
     * FInstancedStruct — no base class required.
     *
     * Channels route; payloads identify. Subscribers branch on the payload's identity field for "what event
     * is this"; the bus's subscriber callback receives the channel separately for "how was this delivered to
     * me." See the SimpleCore architecture notes (MULTI-CHANNEL PUBLISH CONTRACT) for the full contract.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleCore|Signals",
        meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
    static void PublishMessage(UObject* WorldContextObject, FGameplayTag Channel, const FInstancedStruct& Payload);

    /**
     * Publish an event on multiple tag channels treating the call as one logical event instance. Subscribers
     * reached via any channel in the set fire exactly once (default dedup-on, by FDelegateHandle), with the
     * callback's first arg set to the channel from the publish set most specific to that subscriber's bound
     * tag (longest descendant where the bound tag is a prefix; tie-break by input array order).
     *
     * Use when a logical event answers to multiple addresses — for example, a node in a LinkedQuestline graph
     * that has both its standalone tag and its inlining-context tag and broadcasts on both. The payload is
     * delivered identically across every subscriber; only delivery metadata (the matched channel) varies per
     * subscription.
     *
     * bAllChannels=true opts out of dedup: the bus fires once per channel as a naive sibling-publish would.
     * Use for debug auditing, observability surfaces, or genuinely-distinct-scope publishes where every
     * channel must reach its own subscribers.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleCore|Signals",
        meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
    static void PublishMessageOnChannels(UObject* WorldContextObject, const TArray<FGameplayTag>& Channels,
        const FInstancedStruct& Payload, bool bAllChannels = false);

    /**
     * Remove every signal-bus subscription whose listener is the given object. Single-call cleanup for actors /
     * components with many subscriptions across many channels — call from EndPlay or BeginDestroy. Compares raw UObject
     * pointers, so subclasses and unrelated objects are not affected. No-op if Listener is null. Most adopter usage
     * passes self as Listener.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleCore|Signals",
        meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
    static void UnsubscribeListener(UObject* WorldContextObject, UObject* Listener);
    

    // ── World State ────────────────────────────────────────────────────────────────────────────────

    /**
     * Increments the fact's assertion count for Tag. Publishes FWorldStateFactAddedEvent on the 0→1
     * transition by default. Use BroadcastMode to opt into different semantics:
     *   BoundaryOnly (default) — fire only on 0→1 transitions.
     *   Always — fire on every call regardless of count.
     *   Suppress — never fire, even at the boundary.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleCore|World State",
        meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
    static void AddFact(UObject* WorldContextObject, FGameplayTag Tag,
        EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly);

    /**
     * Decrements the fact's assertion count for Tag. Publishes FWorldStateFactRemovedEvent on the 1→0
     * transition by default and removes the entry from the map at that boundary. Use BroadcastMode to
     * opt into different semantics; see AddFact's doc.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleCore|World State",
        meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
    static void RemoveFact(UObject* WorldContextObject, FGameplayTag Tag,
        EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly);

    /**
     * Removes the fact entirely regardless of count. Publishes FWorldStateFactRemovedEvent if the fact was
     * present (unless bSuppressBroadcast=true). Use for hard resets; prefer RemoveFact for paired
     * add/remove patterns where reference-count semantics matter.
     */
    UFUNCTION(BlueprintCallable, Category = "SimpleCore|World State",
        meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
    static void ClearFact(UObject* WorldContextObject, FGameplayTag Tag, bool bSuppressBroadcast = false);

    /** Returns true if the fact's count is greater than zero. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleCore|World State",
        meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
    static bool HasFact(UObject* WorldContextObject, FGameplayTag Tag);

    /**
     * Returns the raw count for this fact. Returns 0 if the fact has never been added or has been fully
     * removed. Suitable for querying how many times a repeatable fact has been asserted.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleCore|World State",
        meta = (WorldContext = "WorldContextObject", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
    static int32 GetFactValue(UObject* WorldContextObject, FGameplayTag Tag);

private:
    /** Resolves the SignalSubsystem from a WorldContext via World → GameInstance → Subsystem. Returns null
     *  on any resolution failure (callers no-op silently — same pattern as USimpleQuestBlueprintLibrary). */
    static USignalSubsystem* GetSignalSubsystem(const UObject* WorldContextObject);

    /** Resolves the WorldStateSubsystem from a WorldContext via the same chain. Same null-on-failure
     *  contract as GetSignalSubsystem. */
    static UWorldStateSubsystem* GetWorldStateSubsystem(const UObject* WorldContextObject);
};