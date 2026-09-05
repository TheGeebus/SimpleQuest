// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "QuickStartSaveTarget.h"
#include "QuickStartSaveSubsystem.generated.h"

class UQuickStartSaveGame;

/**
 * Owns the QuickStart's save and load flow. Demo code rather than plugin API - SimpleQuest deliberately ships no
 * USaveGame and takes no position on save architecture; this is the sample showing one way to compose the pieces it
 * does provide.
 *
 * A GameInstance subsystem because the flow has to survive the level reload a restore performs, and because reaching a
 * SERVICE by type is a smaller dependency than reaching a game-specific GameInstance class - the same way every actor
 * in the demo already reaches quest state through UQuestStateSubsystem.
 */
UCLASS()
class UQuickStartSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Captures quest state, asks every registered target to contribute, and writes the slot. */
	UFUNCTION(BlueprintCallable, Category = "QuickStart|Save")
	void RequestSave(const FString& SlotName);

	/** Reads the slot, stages the restore, and reopens the level the save was taken in. */
	UFUNCTION(BlueprintCallable, Category = "QuickStart|Save")
	void RequestLoad(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "QuickStart|Save")
	bool HasSave(const FString& SlotName) const;

	/**
	 * Announce that this object has state to persist. Call it from BeginPlay: if a restore is staged, the target's data
	 * is applied immediately, so the caller never has to time its registration against the load.
	 *
	 * Returns true when saved state was applied during this call - branch on it to decide whether to initialize
	 * defaults. False means either nothing was staged (a fresh game) or the target was unset, which warns.
	 *
	 * A staged restore is deliberately never cleared, so a target registering long after a load is also handed
	 * save-time state and told it was restored. Fine for actors that spawn at level start; a game spawning targets
	 * mid-session would want the restore consumed or scoped.
	 */
	UFUNCTION(BlueprintCallable, Category = "QuickStart|Save", meta = (ReturnDisplayName = "Restored"))
	bool RegisterSaveTarget(const TScriptInterface<IQuickStartSaveTarget>& Target);

	virtual void Deinitialize() override;

private:
	/**
	 * The save being restored, held from the load until the end of the frame after the level opens, so targets
	 * registering from BeginPlay catch up. Consumed after that: a later registration is a fresh spawn.
	 */
	UPROPERTY()
	TObjectPtr<UQuickStartSaveGame> PendingRestore;

	/** Weak, per the framework's own reference convention, so a destroyed target simply drops out of the walk. */
	TArray<TWeakObjectPtr<UObject>> SaveTargets;

	void ForEachLiveTarget(TFunctionRef<void(UObject&)> Visit);

	void ArmRestoreConsumption();
	void HandleWorldInitForRestore(UWorld* World, const UWorld::InitializationValues IVS);

	FDelegateHandle PostWorldInitHandle;
};

