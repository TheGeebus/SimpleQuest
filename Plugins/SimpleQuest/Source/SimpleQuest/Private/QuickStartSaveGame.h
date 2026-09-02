// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Quests/Types/SimpleQuestSaveSnapshot.h"
#include "QuickStartSaveGame.generated.h"

/**
 * Minimal USaveGame wrapper so the snapshot can round-trip through UGameplayStatics::SaveGameToSlot. Used to persist
 * progress through the sample level provided in the SimpleQuest plugin, QuickStart.
 */
UCLASS()
class UQuickStartSaveGame : public USaveGame
{
	GENERATED_BODY()

public:

	// -----------------------------------------------------------------------------------------------------------------
	//   SimpleQuest save data - add alongside any save data required by your game
	// -----------------------------------------------------------------------------------------------------------------
	
	/**
	 * Embed a property of this type, FSimpleQuestSaveSnapshot, in your game's own USaveGame subclass. To SAVE: create the
	 * save game object, call USimpleQuestBlueprintLibrary::CaptureQuestState, assign the result to this property, and save
	 * the object to a slot.
	 *
	 * To LOAD, hand the snapshot back through one of two entry points:
	 *   - Across a level change (most common): call ApplyQuestSnapshot with bRestoreOnNextLevelLoad = true BEFORE you
	 *     open your gameplay level. The data applies immediately and the live questlines rebuild themselves once the level
	 *     finishes loading - no second call.
	 *   - In place, no level change: call RestoreQuestState - it applies the data and rebuilds the questlines in one step
	 *     in the current level. This brings the quest DATA back, but it does NOT re-fire lifecycle events to observers
	 *     that are already subscribed, so an event-driven HUD, giver, or spawned actor won't refresh on its own (that
	 *     reconstruction runs through catch-up, which fires only when a component first subscribes - in a running level,
	 *     at spawn). Reach for it when your consumers read quest state by querying it (Is Quest Live / Completed,
	 *     resolution history) or when you refresh your presentation yourself; otherwise prefer the level-transition path
	 *     above so the world reconstructs itself on the reload.
	 *
	 * (For manual control across a transition, call ApplyQuestSnapshot with the flag left false before opening the level,
	 * then call RestoreQuestGraphs - which takes no snapshot - from the destination level once it is up.)
	 */
	UPROPERTY(BlueprintReadWrite, SaveGame)
	FSimpleQuestSaveSnapshot Snapshot;

	// -----------------------------------------------------------------------------------------------------------------
	//   Game-specific save data - player positioning and reward totals for the QuickStart tutorial scenario
	// -----------------------------------------------------------------------------------------------------------------

	/**
	 * The level this save was taken in, opened on restore.
	 */
	UPROPERTY(BlueprintReadWrite, SaveGame)
	FName LevelName;
	
	UPROPERTY(BlueprintReadWrite, SaveGame)
	FTransform PlayerTransform;

	UPROPERTY(BlueprintReadWrite, SaveGame)
	FRotator ControlRotation;

	/**
	 * Running reward totals for the HUD. They're attributes owned by the game, not the framework. Grants are never replayed
	 * when a save is restored. They are transient events that may add to the totals, which must be persisted by the game.
	 */
	UPROPERTY(BlueprintReadWrite, SaveGame)
	int32 GoldTotal = 0;

	UPROPERTY(BlueprintReadWrite, SaveGame)
	int32 ExperienceTotal = 0;
};
