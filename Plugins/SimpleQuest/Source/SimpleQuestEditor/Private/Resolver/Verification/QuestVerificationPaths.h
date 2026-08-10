// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Shared constants for the verification tier. Kept apart from the oracle itself because the console commands and the
// comparators both need them, and a copy in each is how the two drift. The export ROOT and per-key FOLDER are NOT
// here: they belong to the pipeline, and the verification tier calls it rather than restating it.

#include "CoreMinimal.h"
#include "Resolver/QuestExportOperations.h"

/**
 * Suffix the round-trip harness gives its reconstructed asset, so the copy compiles into its own tag namespace instead
 * of colliding with the original. Every comparison normalizes it away before diffing.
 */
inline constexpr const TCHAR* GQuestRoundTripSuffix = TEXT("_RT");

/**
 * The compiled-dump file for a sanitized questline ID. Same suffix rule as the folder. The export ROOT it sits beside
 * comes from the pipeline rather than being restated here - the two copies of that derivation had already drifted once.
 */
inline FString QuestCompiledDumpPathFor(const FString& SanitizedID)
{
	return QuestExport_RootDir() / (SanitizedID + TEXT("_compiled_dump.tsv"));
}

