// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The export pipeline as ONE callable operation, so the console command and a toolbar button are both thin callers
// rather than one of them being the implementation. Nothing here logs: outcomes come back as data and the caller
// decides how to say them - the same division that lets the import panel show what a log used to be the only witness
// to. The mirror of QuestImportOperations.

#include "CoreMinimal.h"
#include "QuestMappingSource.h"

class ISimpleQuestDataFormat;
class UQuestImportMapping;
class UQuestlineGraph;

struct FQuestExportRequest
{
	/** The questline to write out. */
	const UQuestlineGraph* Graph = nullptr;

	/**
	 * WHERE it goes, and in what encoding. An EMPTY Folder means the DERIVED destination, which is what keeps every
	 * existing caller byte-identical - the console, the round-trip harness and the fixture chain all pass none.
	 * DESTINATION OWNERSHIP PICKS THE VERB: a folder is ours and is replaced wholesale; a Data Table is a studio's and
	 * is PLANNED rather than written, because rows in an asset we do not own are theirs to approve. Both arrive here.
	 */
	FQuestDataEndpoint Endpoint;

	/** Optional studio-shape restatement. Absent means a canonical export in the plugin's own vocabulary. */
	const UQuestImportMapping* Mapping = nullptr;
};

struct FQuestExportOutcome
{
	/** Where it went. Set even on most refusals, because naming the folder is half of explaining the refusal. */
	FString OutDir;
	FString ExportKey;

	/**
	 * True when the request named no folder and the destination came from the questline's ID. Callers word the receipt
	 * differently: a derived destination is somewhere the user has never navigated to, and worth naming in full.
	 */
	bool bDestinationDerived = false;

	int32 EntityRows = 0;
	int32 TypeCount = 0;
	int32 EdgeCount = 0;
	int32 KnotsCollapsed = 0;
	int32 FilesWritten = 0;
	int32 FilesRemoved = 0;      // what the previous export left that this one replaced

	TArray<FString> Warnings;    // reverse-mapping warnings; never fatal

	/**
	 * Non-empty when nothing was written - a refused destination, an unowned folder, a format conversion, a failed
	 * provider write. Phrased as the caller will present it, because the refusals read differently from each other
	 * and collapsing them into one call must not collapse the messages.
	 */
	FString Error;

	/**
	 * What writing this questline into the destination table WOULD change. Populated only when the endpoint named a
	 * Data Table, and never published from here - this file's contract is that outcomes come back as data and the
	 * caller decides how to say them, so the broker is the caller's to write to for the same reason the log is.
	 */
	FQuestInPlacePlan RowPlan;
	bool bPlanned = false;   // true when a plan was computed instead of an export being written

	/**
	 * The reverse-mapped bundle the plan was computed from. Carried because a Create writes the WHOLE row and the plan
	 * deliberately does not duplicate its cells - so applying needs the very rows the plan was built against. Rebuilding
	 * them at apply time would risk building something subtly different, which is the drift the plan exists to prevent.
	 */
	FQuestDataBundle PlannedBundle;
	
	bool bExported = false;
};

/**
 * WHERE exports go. One definition, because the derivation is about to become a DEFAULT rather than a constraint: the
 * panel seeds its destination field from it, the operation falls back to it when nothing is chosen, and the
 * verification tier addresses the same folders. It has existed TWICE - inline in the run, and in the verification
 * tier's own paths header, whose comment already records its copies drifting once.
 */
FString QuestExport_RootDir();

/** The export folder for an ALREADY-SANITIZED questline key. */
FString QuestExport_FolderForKey(const FString& SanitizedKey);

/**
 * A questline's export KEY - its sanitized effective ID. This string does TWO jobs: it names the destination folder,
 * and it is the questline-self ROW KEY inside the bundle. Only the first becomes chooseable; identity is not a
 * location, which is why the empty-key refusal survives a chosen destination.
 */
FString QuestExport_KeyFor(const UQuestlineGraph& Graph);

/**
 * Where this questline exports when no destination is chosen. EMPTY when the ID reduces to an empty key - callers
 * refuse rather than collapsing onto the export root.
 */
FString QuestExport_DerivedFolderFor(const UQuestlineGraph& Graph);

/**
 * Walk the questline into a bundle, optionally restate it through a recipe, and write it to its export folder -
 * claiming ownership of that folder and refusing rather than overwriting one that belongs to something else.
 * Writes through a staging directory: nothing is deleted until the replacement exists on disk, so a failed or
 * refused run leaves the previous export exactly as it was.
 */
bool QuestExport_Run(const FQuestExportRequest& Request, FQuestExportOutcome& Out);

