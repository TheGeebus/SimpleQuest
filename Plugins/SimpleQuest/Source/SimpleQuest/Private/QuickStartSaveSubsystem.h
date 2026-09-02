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
	 * is applied immediately, so the caller never has to ask whether a load is in progress.
	 */
	UFUNCTION(BlueprintCallable, Category = "QuickStart|Save")
	void RegisterSaveTarget(const TScriptInterface<IQuickStartSaveTarget>& Target);

private:
	/**
	 * The save being restored, held across the level reload - which is the whole reason this lives on the GameInstance.
	 * Deliberately NOT cleared once applied, so a target registering later still catches up; a subsequent load replaces
	 * it. Simplification worth knowing: an actor spawned long after a load would also be handed save-time state.
	 */
	UPROPERTY()
	TObjectPtr<UQuickStartSaveGame> PendingRestore;

	/** Weak, per the framework's own reference convention, so a destroyed target simply drops out of the walk. */
	TArray<TWeakObjectPtr<UObject>> SaveTargets;

	void ForEachLiveTarget(TFunctionRef<void(UObject&)> Visit);
};

