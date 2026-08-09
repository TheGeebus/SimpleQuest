// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The export folder's ownership protocol. An export folder holds exactly ONE export, so a re-export must remove what the
// previous one left - and path derivation is many-to-one, since two questline IDs can sanitize to the same segment, so a
// name check can never answer "is this ours to replace?". A marker file answers it. Separate from the export command
// because a toolbar Export button needs exactly this and nothing else.

#include "CoreMinimal.h"

/** Name of the marker file an export writes into its output folder. */
extern const TCHAR* const GQuestExportMarkerName;

/** What a previous export recorded about itself. Files is the ONLY set a replacement may remove. */
struct FQuestExportMarker
{
	FString Format;
	FString SourceAsset;
	TArray<FString> Files;   // what the previous export wrote — the ONLY things a replacement may remove
};

/** Read the marker from Folder. False when there is none, which means the folder is not ours to replace. */
bool ReadQuestExportMarker(const FString& Folder, FQuestExportMarker& Out);

/** Write the marker into Folder, forcing UTF-8 so it stays readable in a plain editor. False on write failure. */
bool WriteQuestExportMarker(const FString& Folder, const FQuestExportMarker& Marker);

/** Filenames directly in a folder (never recursive). Empty when the folder doesn't exist. */
TArray<FString> QuestExportFilesIn(const FString& Folder);

