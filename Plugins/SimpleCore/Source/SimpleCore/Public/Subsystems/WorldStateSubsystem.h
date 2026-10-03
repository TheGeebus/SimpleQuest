// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/SignalSubsystem.h"
#include "Events/SignalEventBase.h"
#include "WorldStateSubsystem.generated.h"

UENUM(BlueprintType)
enum class EFactBroadcastMode : uint8
{
    /** default - fire only on 0→1 or 1→0 */
    BoundaryOnly    UMETA(DisplayName = "Boundary Only"),
    /** fire on every call regardless of count */
    Always          UMETA(DisplayName = "Always"),
    /** never fire, even at boundary */
    Suppress        UMETA(DisplayName = "Suppress"),        
};

USTRUCT(BlueprintType)
struct SIMPLECORE_API FWorldStateFactAddedEvent : public FSignalEventBase
{
    GENERATED_BODY()

    FWorldStateFactAddedEvent() = default;

    explicit FWorldStateFactAddedEvent(const FGameplayTag InStateTag, const bool bInCatchUp = false)
        : StateTag(InStateTag)
        , bCatchUp(bInCatchUp)
    {}

    UPROPERTY(BlueprintReadWrite)
    FGameplayTag StateTag;

    /**
     * True when this delivery is a replay of a fact that was ALREADY true when the subscriber arrived, rather than a
     * live transition. Only SubscribeToFactAdded produces these, and only to the subscriber that just arrived - a
     * replay is never broadcast to anyone else.
     *
     * Read it as "this is already true", not "this just happened". A handler that plays a one-shot - a sound, a
     * flourish, a popup - should skip a catch-up delivery; a handler that sets state should run either way.
     */
    UPROPERTY(BlueprintReadOnly)
    bool bCatchUp = false;
};

USTRUCT(BlueprintType)
struct SIMPLECORE_API FWorldStateFactRemovedEvent : public FSignalEventBase
{
    GENERATED_BODY()

    FWorldStateFactRemovedEvent() = default;

    explicit FWorldStateFactRemovedEvent(const FGameplayTag InStateTag)
        : StateTag(InStateTag)
    {}

    UPROPERTY(BlueprintReadWrite)
    FGameplayTag StateTag;
};

/** Multicast fired after any mutation to WorldFacts. See UWorldStateSubsystem::OnAnyFactChanged for semantics. */
DECLARE_MULTICAST_DELEGATE(FOnAnyFactChanged);

UCLASS()
class SIMPLECORE_API UWorldStateSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /**
     * Increments the fact's count. Publishes FWorldStateFactAddedEvent only on the 0-to-1 transition by default.
     *
     * Public C++ surface only - BP code reaches this via USimpleCoreBlueprintLibrary::AddFact, which handles the
     * WorldContext → World → GameInstance → Subsystem resolution.
     */
    void AddFact(FGameplayTag Tag, EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly);

    /**
     * Decrements the fact's count. Publishes FWorldStateFactRemovedEvent and removes the entry only on 1-to-0 transition
     * by default.
     *
     * Public C++ surface only - BP code reaches this via USimpleCoreBlueprintLibrary::RemoveFact.
     */
    void RemoveFact(FGameplayTag Tag, EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly);

    /**
     * Removes the fact entirely regardless of count. Publishes FWorldStateFactRemovedEvent if the fact was present. Use
     * for hard resets; prefer RemoveFact for paired add/remove patterns.
     *
     * Public C++ surface only - BP code reaches this via USimpleCoreBlueprintLibrary::ClearFact.
     */
    void ClearFact(FGameplayTag Tag, bool bSuppressBroadcast = false);
    
    /**
     * Returns true if the fact's count is greater than zero.
     *
     * Public C++ surface only - BP code reaches this via USimpleCoreBlueprintLibrary::HasFact.
     */
    bool HasFact(FGameplayTag Tag) const;

    /**
     * Returns the raw count for this fact. Returns 0 if the fact has never been added or has been fully removed. Suitable
     * for querying how many times a repeatable fact has been asserted.
     *
     * Public C++ surface only - BP code reaches this via USimpleCoreBlueprintLibrary::GetFactValue.
     */
    int32 GetFactValue(FGameplayTag Tag) const;

    /**
     * Read-only view of all current facts. Intended for debug / inspection surfaces (editor panels during PIE, save-game
     * serializers). Returns a const reference to the internal map - no copy, no allocation. Callers must not mutate;
     * fact mutation always goes through AddFact / RemoveFact / ClearFact to keep broadcast semantics intact.
     */
    const TMap<FGameplayTag, int32>& GetAllFacts() const { return WorldFacts; }

    /**
     * Every fact currently true at or under Channel, honoring the same routing rule the bus uses on delivery, sorted
     * so the order is stable from run to run. This is the enumeration behind SubscribeToFactAdded's catch-up; it is
     * public because the Blueprint path needs the identical list and must not re-derive it.
     *
     * Returns a snapshot by value on purpose - a handler is free to add or remove facts while consuming it, and
     * iterating WorldFacts directly across that would be undefined.
     */
    void GetFactsMatching(FGameplayTag Channel, ESignalRoutingMode Routing, TArray<FGameplayTag>& OutFacts) const;

    /**
     * Subscribe to a fact becoming true - and be told immediately about the facts that are true already.
     *
     * The ordinary bus subscription only ever hears what happens next, so a component that spawns into a world where
     * the power is already on hears nothing and has to go and ask. This asks on its behalf: every matching fact that
     * is already true is delivered first, flagged bCatchUp, then the live subscription is wired. Both happen inside
     * this call, so no publish can land between them and be seen twice.
     *
     * A catch-up delivery goes only to the arriving subscriber - nothing is published and no other subscriber on the
     * channel sees anything. The callback's channel argument is the specific fact's tag, exactly as it would be on a
     * live delivery to a subscriber bound further up the hierarchy.
     *
     * *** REMOVAL IS NOT REPLAYED, and that is deliberate rather than an omission. *** A fact being absent is the
     * state of nearly every tag that exists, and a removal that already happened is a moment that has passed -
     * delivering it late would assert something just happened when it did not. If "this was true once" matters to
     * you, record that as its own fact: SimpleQuest's append-only Started anchor is that pattern, and its Held leaf
     * is the deliberate counter-example, drained before every save because persisting a pause would restore a game
     * that looks stuck.
     *
     * Public C++ surface only - BP code reaches this via USimpleCoreBlueprintLibrary::SubscribeToFactAdded.
     */
    template<typename ListenerType>
    FDelegateHandle SubscribeToFactAdded(
        FGameplayTag Channel, 
        ListenerType* Listener, 
        void(ListenerType::* Function)(FGameplayTag, const FWorldStateFactAddedEvent&), 
        ESignalRoutingMode Routing = FSignalRoutingDefaults::HierarchicalSubscribe);

    /**
     * Bulk-overwrites the entire fact map - the save-restore counterpart to GetAllFacts. Replaces all current
     * facts in one shot and fires OnAnyFactChanged once; per-tag FWorldStateFactAdded/RemovedEvents are NOT
     * published, since restore is a bulk state swap, not a sequence of gameplay transitions (post-load
     * subscribers re-read via the late-registration catch-up).
     */
    void RestoreFacts(const TMap<FGameplayTag, int32>& InFacts);

    /** 
     * Multicast fired after any mutation to WorldFacts (AddFact, RemoveFact, or ClearFact), regardless of the
     * per-call broadcast mode. Distinct from the per-tag FWorldStateFactAdded/RemovedEvent publishes - this
     * is a "registry mutated, refresh if you care about the whole map" signal for inspection surfaces (Facts
     * panel, future telemetry tools). Fires synchronously inside the mutation method, after the per-tag
     * publish (if any). FSimpleMulticastDelegate semantics - bind via AddRaw, unbind via Remove. 
     */
    FOnAnyFactChanged OnAnyFactChanged;

private:
    /** 
     * Live game world state. Keys are gameplay tags; values are assertion counts. A fact is considered present when its
     * count > 0. Use AddFact/RemoveFact for paired patterns; ClearFact for hard resets.
     */
    UPROPERTY()
    TMap<FGameplayTag, int32> WorldFacts;
};


template<typename ListenerType>
FDelegateHandle UWorldStateSubsystem::SubscribeToFactAdded(
    const FGameplayTag Channel, 
    ListenerType* Listener, 
    void(ListenerType::* Function)(FGameplayTag, const FWorldStateFactAddedEvent&), 
    const ESignalRoutingMode Routing)
{
    USignalSubsystem* Signals = GetGameInstance() ? GetGameInstance()->GetSubsystem<USignalSubsystem>() : nullptr;
    if (!Signals || !Listener || !Channel.IsValid() || !Function) return FDelegateHandle();

    TArray<FGameplayTag> AlreadyTrue;
    GetFactsMatching(Channel, Routing, AlreadyTrue);

    // Catch-up runs BEFORE the subscription is wired. Wire-first would let a publish landing between the two steps
    // reach the listener twice - once live, once in the replay. Nothing can interleave here: one synchronous call,
    // no tick boundary. A handler is still free to re-enter, which is why the list above is a snapshot and why the
    // listener is re-checked each time round.
    TWeakObjectPtr<ListenerType> WeakListener(Listener);
    for (const FGameplayTag& FactTag : AlreadyTrue)
    {
        if (!WeakListener.IsValid()) break;
        (WeakListener.Get()->*Function)(FactTag, FWorldStateFactAddedEvent(FactTag, true));
    }

    return Signals->SubscribeMessage<FWorldStateFactAddedEvent>(Channel, Listener, Function, Routing);
}

