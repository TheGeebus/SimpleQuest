// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The export folder's ownership protocol, and the folder's own account of itself. An export folder holds exactly ONE export,
// so a re-export must remove what the previous one left - and path derivation is many-to-one, since two questline IDs can
// sanitize to the same segment, so a name check can never answer "is this ours to replace?". A marker file answers it. It
// also records what the folder was written FROM and THROUGH, so a reader arriving with nothing but the folder - a build
// server, a batch validator, a teammate's checkout - can re-derive the export instead of needing editor state it does not
// have. Separate from the export command because a toolbar Export button needs exactly this and nothing else.

#include "CoreMinimal.h"

/** Name of the marker file an export writes into its output folder. */
extern const TCHAR* const GQuestExportMarkerName;

/** What a previous export recorded about itself. Files is the ONLY set a replacement may remove. */
struct FQuestExportMarker
{
	FString Format;
	FString SourceAsset;

	/**
	 * The recipe this export was written THROUGH; empty when it was canonical. It belongs beside the data rather than in
	 * editor state, because a folder in somebody's repository has to be able to say how it should be READ - and a recipe
	 * remembered only on the machine that wrote it makes the corpus unreadable to everyone else, including a build server,
	 * which has no editor state at all. A marker with no Mapping line reads as canonical.
	 */
	FString Mapping;

	TArray<FString> Files;   // what the previous export wrote - the ONLY things a replacement may remove
};

/** Read the marker from Folder. False when there is none, which means the folder is not ours to replace. */
bool ReadQuestExportMarker(const FString& Folder, FQuestExportMarker& Out);

/** Write the marker into Folder, forcing UTF-8 so it stays readable in a plain editor. False on write failure. */
bool WriteQuestExportMarker(const FString& Folder, const FQuestExportMarker& Marker);

/** Filenames directly in a folder (never recursive). Empty when the folder doesn't exist. */
TArray<FString> QuestExportFilesIn(const FString& Folder);

