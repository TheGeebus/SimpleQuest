// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#pragma once

#include "CoreMinimal.h"
#include "SimpleQuestSaveLoadTest.generated.h"

/**
 * Throwaway fingerprint payload for the save/load test rig. Lives in a header (not the .cpp) only because FInstancedStruct::Make
 * and GetPtr need UnrealHeaderTool to generate StaticStruct() reflection, and UHT scans headers, never .cpp files. Delete this
 * alongside SimpleQuestSaveLoadTest.cpp — the pair is one removable unit.
 */
USTRUCT()
struct FSimpleQuestSaveLoadTestData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	int32 Marker = 0;

	UPROPERTY(SaveGame)
	FString Note;
};