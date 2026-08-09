// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestExportOperations.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestBundleTransforms.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/QuestExportOutput.h"
#include "Resolver/QuestGraphExport.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Utilities/SimpleQuestEditorUtils.h"

bool QuestExport_Run(const FQuestExportRequest& Request, FQuestExportOutcome& Out)
{
	const UQuestlineGraph* Graph = Request.Graph;
	if (!Graph || !Graph->QuestlineEdGraph)
	{
		Out.Error = TEXT("no questline asset, or it has no authored graph.");
		return false;
	}

	FQuestDataBundle Bundle;
	const TUniquePtr<FQuestlineGraphTraversalPolicy> Policy = MakeUnique<FQuestlineGraphTraversalPolicy>();

	// Questline-self row: the asset's own authored fields (QuestlineID / DisplayName / Description / DisplayData /
	// ResettableReplay as columns; QuestlineRewards explodes through the instanced recursion into reward child rows).
	// Keyed by the SANITIZED EffectiveID — the same segment form compiled tags use, so the export key aligns with
	// tag identity and stays interchange-safe (no spaces/punctuation in keys or folder names).
	const FString SelfKey = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Graph->GetEffectiveID());
	Out.ExportKey = SelfKey;

	// The key can come out EMPTY from input a designer can type: a whitespace-only QuestlineID is not IsEmpty(), so the
	// asset-name fallback never fires, and the sanitizer trims it to nothing. An empty segment appends only a separator,
	// so the destination would collapse to the export ROOT and scatter this export across every other questline's output.
	// Refuse rather than write somewhere unintended, and name the field to fix.
	if (SelfKey.IsEmpty())
	{
		Out.Error = FString::Printf(TEXT("'%s' has a QuestlineID that reduces to an empty export key (raw value: '%s'). "
			"Give it at least one letter, digit or underscore — or clear the field entirely to fall back to the asset "
			"name. Nothing exported."),
			*Graph->GetPathName(),
			*Graph->GetEffectiveID());
		return false;
	}
	CollectQuestEntityRow(Graph, SelfKey, {}, Bundle);

	CollectQuestGraphBundle(Graph->QuestlineEdGraph, TEXT("root"), *Policy, Bundle);

	// Optional studio-shape restatement. Absent = canonical export (our vocabulary), byte-identical to before.
	if (Request.Mapping)
	{
		TMap<FString, FString> SourceKeyByGuid;
		TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
		CollectQuestNodeIdentity(Graph->QuestlineEdGraph, SourceKeyByGuid, NodeByGuid);
		QuestBundle_ApplyReverseMapping(Bundle, *Request.Mapping, SourceKeyByGuid, NodeByGuid, Out.Warnings);
	}

	// Prove containment structurally instead of trusting the string that produced it — the destination must be exactly one
	// level below the export root. Holds even if the key derivation changes or is later fed from somewhere new.
	const FString ExportRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport"));
	const FString OutDir = FPaths::ConvertRelativePathToFull(ExportRoot / SelfKey);
	// Set BEFORE the guards, so a caller reporting a refusal can still name where it would have gone.
	Out.OutDir = OutDir;
	{
		FString NormRoot = ExportRoot;  FPaths::NormalizeDirectoryName(NormRoot);
		FString NormOut  = OutDir;      FPaths::NormalizeDirectoryName(NormOut);
		if (NormOut == NormRoot || FPaths::GetPath(NormOut) != NormRoot)
		{
			Out.Error = FString::Printf(TEXT("refusing — destination '%s' is not a direct child of the export root '%s' "
				"(export key '%s'). Nothing exported."),
				*NormOut,
				*NormRoot,
				*SelfKey);
			return false;
		}
	}

	// Resolved from a NAME rather than handed in as a provider, so a caller never holds one - the console reads it from
	// --format, the toolbar from a combo, and neither has to know the registry exists.
	const TUniquePtr<ISimpleQuestDataFormat> Format = FQuestDataFormatRegistry::Get().Create(Request.FormatName);
	if (!Format)
	{
		Out.Error = FString::Printf(TEXT("no data format named '%s' is registered. Registered: %s. Nothing exported."),
			*Request.FormatName,
			*FString::Join(FQuestDataFormatRegistry::Get().GetRegisteredNames(), TEXT(", ")));
		return false;
	}

	// OWNERSHIP — never replace a folder we didn't write. This is the guard that survives a NAME COLLISION: two
	// questline IDs can sanitize to one folder, and a hand-authored source folder sitting at that name would otherwise
	// be overwritten by an export.
	FQuestExportMarker Previous;
	const bool bHadMarker = ReadQuestExportMarker(OutDir, Previous);
	const TArray<FString> Existing = QuestExportFilesIn(OutDir);
	if (Existing.Num() > 0 && !bHadMarker)
	{
		Out.Error = FString::Printf(TEXT("refusing — '%s' already holds %d file(s) and carries no SimpleQuest export "
			"marker, so an export did not write it. Exporting would replace its contents. Move or delete that folder, "
			"or give this questline a different QuestlineID. Nothing written."),
			*OutDir,
			Existing.Num());
		return false;
	}
	// The marker may predate the normalization below, and "same asset, other spelling" must not read as "different
	// questline". That refusal tells a designer to change their QuestlineID, which would be wrong.
	if (bHadMarker && !Previous.SourceAsset.IsEmpty()
		&& FSoftObjectPath(Previous.SourceAsset).GetAssetPathString() != Graph->GetPathName())
	{
		Out.Error = FString::Printf(TEXT("refusing — '%s' holds the export of a DIFFERENT questline ('%s'). Their IDs "
			"reduce to the same folder name, so each would overwrite the other. Give one a distinct QuestlineID. "
			"Nothing written."),
			*OutDir,
			*Previous.SourceAsset);
		return false;
	}

	// A folder holds ONE export, and its format is part of what it holds - re-exporting the same questline in a
	// different format does not update this folder, it CONVERTS it, deleting every file the previous format wrote.
	// The marker has always recorded the format; this is the guard finally reading it. Refused rather than prompted,
	// for the same reason as the two above: an export that silently replaces a folder's contents is the failure this
	// whole marker protocol exists to prevent, and "same questline" does not make the replacement harmless.
	if (bHadMarker && !Previous.Format.IsEmpty() && !Previous.Format.Equals(Format->FormatName(), ESearchCase::IgnoreCase))
	{
		Out.Error = FString::Printf(TEXT("refusing — '%s' holds a %s export of this questline and you asked for %s. "
			"Exporting would delete the %s files and replace them. Export to a different folder, or delete this one "
			"first. Nothing written."),
			*OutDir,
			*Previous.Format,
			*Format->FormatName(),
			*Previous.Format);
		return false;
	}

	// STAGE — write the complete new export beside the destination. NOTHING is deleted until it exists on disk, so a
	// failed, refused or interrupted write leaves the previous export exactly as it was. The sanitizer can never emit a
	// '.', so this name cannot collide with a real destination.
	const FString Staging = OutDir + TEXT(".incoming");
	IFileManager::Get().DeleteDirectory(*Staging, /*RequireExists*/ false, /*Tree*/ true);
	if (!Format->WriteBundle(Bundle, Staging))
	{
		IFileManager::Get().DeleteDirectory(*Staging, false, true);
		Out.Error = FString::Printf(TEXT("the %s provider failed to write. '%s' is unchanged."),
			*Format->FormatName(),
			*OutDir);
		return false;
	}

	FQuestExportMarker Marker;
	Marker.Format = Format->FormatName();
	// The asset's own path, which is already canonical - unlike a console argument, which the caller may have typed in
	// either the short or the object-path form.
	Marker.SourceAsset = Graph->GetPathName();
	Marker.Files = QuestExportFilesIn(Staging);   // enumerated, not reported — works for any provider, including one that ignores us

	// REPLACE — remove only what the PREVIOUS export recorded. Never a directory, never read-only: a read-only file is
	// protected on purpose, and a subdirectory can't contribute to the stale-shape problem because the reader doesn't
	// recurse. Any failure aborts with the finished copy left in place and named.
	IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
	int32 Removed = 0;
	for (const FString& Old : Previous.Files)
	{
		const FString OldPath = OutDir / Old;
		if (!IFileManager::Get().FileExists(*OldPath)) continue;
		if (!IFileManager::Get().Delete(*OldPath, /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ false))
		{
			Out.Error = FString::Printf(TEXT("couldn't remove '%s' from the previous export — it may be read-only or "
				"open elsewhere. '%s' is unchanged; the finished new export is at '%s'."),
				*OldPath,
				*OutDir,
				*Staging);
			return false;
		}
		++Removed;
	}
	if (bHadMarker) { IFileManager::Get().Delete(*(OutDir / GQuestExportMarkerName), false, false, false); }

	WriteQuestExportMarker(Staging, Marker);
	for (const FString& New : Marker.Files)
	{
		if (!IFileManager::Get().Move(*(OutDir / New), *(Staging / New)))
		{
			Out.Error = FString::Printf(TEXT("couldn't move '%s' into place — '%s' is now PARTIAL and should not be "
				"imported. The complete export is at '%s'."),
				*New,
				*OutDir,
				*Staging);
			return false;
		}
	}
	IFileManager::Get().Move(*(OutDir / GQuestExportMarkerName), *(Staging / GQuestExportMarkerName));
	IFileManager::Get().DeleteDirectory(*Staging, false, true);   // scratch only; a failure here is not data loss

	int32 RowTotal = 0;
	for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		RowTotal += TablePair.Value.Rows.Num();
	}
	Out.EntityRows     = RowTotal;
	Out.TypeCount      = Bundle.TablesByType.Num();
	Out.EdgeCount      = Bundle.Edges.Num();
	Out.KnotsCollapsed = Bundle.KnotsCollapsed;
	Out.FilesWritten   = Marker.Files.Num();
	Out.FilesRemoved   = Removed;
	Out.bExported      = true;
	return true;
}

