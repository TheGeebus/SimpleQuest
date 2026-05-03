// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WorldStateSubsystem.generated.h"

UENUM(BlueprintType)
enum class EFactBroadcastMode : uint8
{
    /** default — fire only on 0→1 or 1→0 */
    BoundaryOnly    UMETA(DisplayName = "Boundary Only"),
    /** fire on every call regardless of count */
    Always          UMETA(DisplayName = "Always"),
    /** never fire, even at boundary */
    Suppress        UMETA(DisplayName = "Suppress"),        
};

USTRUCT(BlueprintType)
struct SIMPLECORE_API FWorldStateFactAddedEvent
{
    GENERATED_BODY()

    FWorldStateFactAddedEvent() = default;

    explicit FWorldStateFactAddedEvent(const FGameplayTag InStateTag)
        : StateTag(InStateTag)
    {}

    UPROPERTY(BlueprintReadWrite)
    FGameplayTag StateTag;
};

USTRUCT(BlueprintType)
struct SIMPLECORE_API FWorldStateFactRemovedEvent
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
    /** Increments the fact's count. Publishes FWorldStateFactAddedEvent only on the 0-to-1 transition by default. */
    UFUNCTION(BlueprintCallable)
    void AddFact(FGameplayTag Tag, EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly);

    /** Decrements the fact's count. Publishes FWorldStateFactRemovedEvent and removes the entry only on 1-to-0 transition by default. */
    UFUNCTION(BlueprintCallable)
    void RemoveFact(FGameplayTag Tag, EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly);

    /**
     * Removes the fact entirely regardless of count. Publishes FWorldStateFactRemovedEvent if the fact was present. Use
     * for hard resets; prefer RemoveFact for paired add/remove patterns.
     */
    UFUNCTION(BlueprintCallable)
    void ClearFact(FGameplayTag Tag, bool bSuppressBroadcast = false);
    
    /** Returns true if the fact's count is greater than zero. */
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool HasFact(FGameplayTag Tag) const;

    /**
     * Returns the raw count for this fact. Returns 0 if the fact has never been added or has been fully removed. Suitable
     * for querying how many times a repeatable fact has been asserted.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetFactValue(FGameplayTag Tag) const;

    /**
     * Read-only view of all current facts. Intended for debug / inspection surfaces (editor panels during PIE, save-game
     * serializers). Returns a const reference to the internal map — no copy, no allocation. Callers must not mutate;
     * fact mutation always goes through AddFact / RemoveFact / ClearFact to keep broadcast semantics intact.
     */
    const TMap<FGameplayTag, int32>& GetAllFacts() const { return WorldFacts; }

    /** Multicast fired after any mutation to WorldFacts (AddFact, RemoveFact, or ClearFact), regardless of the
     *  per-call broadcast mode. Distinct from the per-tag FWorldStateFactAdded/RemovedEvent publishes — this
     *  is a "registry mutated, refresh if you care about the whole map" signal for inspection surfaces (Facts
     *  Panel, future telemetry tools). Fires synchronously inside the mutation method, after the per-tag
     *  publish (if any). FSimpleMulticastDelegate semantics — bind via AddRaw, unbind via Remove. */
    FOnAnyFactChanged OnAnyFactChanged;

private:
    /** Live game world state. Keys are gameplay tags; values are assertion counts. A fact is considered present when its
     * count > 0. Use AddFact/RemoveFact for paired patterns; ClearFact for hard resets.
     */
    UPROPERTY()
    TMap<FGameplayTag, int32> WorldFacts;
};

