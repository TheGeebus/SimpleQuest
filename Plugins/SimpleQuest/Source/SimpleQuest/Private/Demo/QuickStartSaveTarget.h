// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "QuickStartSaveTarget.generated.h"

class UQuickStartSaveGame;

UINTERFACE(MinimalAPI, BlueprintType)
class UQuickStartSaveTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by anything with state to persist. An actor registers itself with UQuickStartSaveSubsystem and is asked
 * to contribute on save and to take its data back on restore.
 *
 * *** THE POINT IS THAT NOTHING CASTS. *** The subsystem holds the save and drives the flow; it never learns what a
 * pawn is, and an implementer never learns what a game instance is. Adding a second thing worth persisting - a
 * container, a door, an NPC - means implementing this and registering, with no edit to the save flow at all.
 *
 * Each implementer reads and writes ITS OWN fields on the save object. That couples an implementer to the save class,
 * which is the honest trade for a demo: "here is the save, take what is yours."
 */
class IQuickStartSaveTarget
{
	GENERATED_BODY()

public:
	/** Write this object's state into the save. Called on every registered target before the slot is written. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "QuickStart|Save")
	void CaptureSaveData(UQuickStartSaveGame* SaveObject);

	/**
	 * Take this object's state back out of the save. Called when a restore is staged and this target registers - which
	 * on a level reload is the target's own BeginPlay, so it does not need to know whether a load is in flight.
	 *
	 * Treat SaveObject as read-only here; it is non-const only because Blueprint handles a plain object reference more
	 * gracefully than a const one.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "QuickStart|Save")
	void ApplySaveData(UQuickStartSaveGame* SaveObject);
};

