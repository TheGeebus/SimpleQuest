// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Shared constants for the verification tier. Kept apart from the oracle itself because the console commands and the
// comparators both need them, and a copy in each is how the two drift.

#include "CoreMinimal.h"

/**
 * Suffix the round-trip harness gives its reconstructed asset, so the copy compiles into its own tag namespace instead
 * of colliding with the original. Every comparison normalizes it away before diffing.
 */
inline constexpr const TCHAR* GQuestRoundTripSuffix = TEXT("_RT");

/**
 * Where the verification tier's artifacts live. One definition because the dump command, the round-trip harness and the
 * compare-only seam all derive the same paths, and three copies is how they drift - one of them already built its root
 * without resolving to a full path while the others did.
 */
inline FString QuestExportRootDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport"));
}

/** The export folder for a sanitized questline ID. Append GQuestRoundTripSuffix to the ID for the reconstructed copy. */
inline FString QuestExportFolderFor(const FString& SanitizedID)
{
	return QuestExportRootDir() / SanitizedID;
}

/** The compiled-dump file for a sanitized questline ID. Same suffix rule as the folder. */
inline FString QuestCompiledDumpPathFor(const FString& SanitizedID)
{
	return QuestExportRootDir() / (SanitizedID + TEXT("_compiled_dump.tsv"));
}

