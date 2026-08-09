// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The export pipeline as ONE callable operation, so the console command and a toolbar button are both thin callers
// rather than one of them being the implementation. Nothing here logs: outcomes come back as data and the caller
// decides how to say them - the same division that lets the import panel show what a log used to be the only witness
// to. The mirror of QuestImportOperations.

#include "CoreMinimal.h"

class ISimpleQuestDataFormat;
class UQuestImportMapping;
class UQuestlineGraph;

struct FQuestExportRequest
{
	/** The questline to write out. */
	const UQuestlineGraph* Graph = nullptr;

	/** Registered provider name. Resolved through the registry inside the run, so a caller never holds a provider. */
	FString FormatName;

	/** Optional studio-shape restatement. Absent means a canonical export in the plugin's own vocabulary. */
	const UQuestImportMapping* Mapping = nullptr;
};

struct FQuestExportOutcome
{
	/** Where it went. Set even on most refusals, because naming the folder is half of explaining the refusal. */
	FString OutDir;
	FString ExportKey;

	int32 EntityRows = 0;
	int32 TypeCount = 0;
	int32 EdgeCount = 0;
	int32 KnotsCollapsed = 0;
	int32 FilesWritten = 0;
	int32 FilesRemoved = 0;      // what the previous export left that this one replaced

	TArray<FString> Warnings;    // reverse-mapping warnings; never fatal

	/** Non-empty when nothing was written - a refused destination, an unowned folder, a format conversion, a failed
	 *  provider write. Phrased as the caller will present it, because the refusals read differently from each other
	 *  and collapsing them into one call must not collapse the messages. */
	FString Error;

	bool bExported = false;
};

/**
 * Walk the questline into a bundle, optionally restate it through a recipe, and write it to its export folder -
 * claiming ownership of that folder and refusing rather than overwriting one that belongs to something else.
 * Writes through a staging directory: nothing is deleted until the replacement exists on disk, so a failed or
 * refused run leaves the previous export exactly as it was.
 */
bool QuestExport_Run(const FQuestExportRequest& Request, FQuestExportOutcome& Out);

