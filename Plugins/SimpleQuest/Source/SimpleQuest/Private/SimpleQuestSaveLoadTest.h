// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// THROWAWAY verification harness for save/load Slice 1 — delete this file and its .cpp once the round-trip is confirmed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Quests/Types/SimpleQuestSaveSnapshot.h"
#include "SimpleQuestSaveLoadTest.generated.h"

/** Minimal USaveGame wrapper so the snapshot can round-trip through UGameplayStatics::SaveGameToSlot. */
UCLASS()
class USimpleQuestSaveLoadTestSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	FSimpleQuestSaveSnapshot Snapshot;
};