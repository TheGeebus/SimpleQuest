// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


// PROTOTYPE — Resolver, Phase 2 authored-graph export. Serializes a questline's AUTHORED model as the interlingua
// folder: one entity table per node/sub-object type (reflection-driven — every EditAnywhere non-Transient UPROPERTY;
// instanced sub-objects explode to child rows in their own type tables) plus one knot-collapsed edge table where
// routing, prereq wiring, deactivation, and nesting are all {from, type, to}. Quest containers' inner graphs recurse;
// LinkedQuestline placements do NOT (the LinkedGraph soft path column is the cross-folder foreign key — the linked
// asset's content belongs to its own export). This is the lossless-structured interlingua form (NOT a readable
// projection): machine fields expected, prettiness is a later panel concern. Read-only, console-triggered. Not shipped API.

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestBundleTransforms.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestExportOutput.h"
#include "Resolver/QuestGraphExport.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	void ExportQuestlineCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ExportQuestline: usage 'SimpleQuest.ExportQuestline <QuestlineAssetPath>'."));
			return;
		}
		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ExportQuestline: couldn't load questline asset or its authored graph '%s'."), *Args[0]);
			return;
		}

		FQuestDataBundle Bundle;
		const TUniquePtr<FQuestlineGraphTraversalPolicy> Policy = MakeUnique<FQuestlineGraphTraversalPolicy>();

		// Questline-self row: the asset's own authored fields (QuestlineID / DisplayName / Description / DisplayData /
		// ResettableReplay as columns; QuestlineRewards explodes through the instanced recursion into reward child rows).
		// Keyed by the SANITIZED EffectiveID — the same segment form compiled tags use, so the export key aligns with
		// tag identity and stays interchange-safe (no spaces/punctuation in keys or folder names).
		const FString SelfKey = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Graph->GetEffectiveID());
		
		// The key can come out EMPTY from input a designer can type: a whitespace-only QuestlineID is not IsEmpty(), so the
		// asset-name fallback never fires, and the sanitizer trims it to nothing. An empty segment appends only a separator,
		// so the destination would collapse to the export ROOT and scatter this export across every other questline's output.
		// Refuse rather than write somewhere unintended, and name the field to fix.
		if (SelfKey.IsEmpty())
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: '%s' has a QuestlineID that reduces to an empty export key "
				"(raw value: '%s'). Give it at least one letter, digit or underscore — or clear the field entirely to fall back "
				"to the asset name. Nothing exported."),
				*Args[0],
				*Graph->GetEffectiveID());
			return;
		}
		CollectQuestEntityRow(Graph, SelfKey, {}, Bundle);

		CollectQuestGraphBundle(Graph->QuestlineEdGraph, TEXT("root"), *Policy, Bundle);

		// Optional studio-shape restatement. Absent = canonical export (our vocabulary), byte-identical to before.
		TArray<FString> Warnings;
		if (const UQuestImportMapping* Mapping = LoadQuestMappingArg(Args))
		{
			TMap<FString, FString> SourceKeyByGuid;
			TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
			CollectQuestNodeIdentity(Graph->QuestlineEdGraph, SourceKeyByGuid, NodeByGuid);
			QuestBundle_ApplyReverseMapping(Bundle, *Mapping, SourceKeyByGuid, NodeByGuid, Warnings);
		}
		for (const FString& W : Warnings) { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ExportQuestline: %s"), *W); }
		
		// Prove containment structurally instead of trusting the string that produced it — the destination must be exactly one
		// level below the export root. Holds even if the key derivation changes or is later fed from somewhere new.
		const FString ExportRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport"));
		const FString OutDir = FPaths::ConvertRelativePathToFull(ExportRoot / SelfKey);
		{
			FString NormRoot = ExportRoot;  FPaths::NormalizeDirectoryName(NormRoot);
			FString NormOut  = OutDir;      FPaths::NormalizeDirectoryName(NormOut);
			if (NormOut == NormRoot || FPaths::GetPath(NormOut) != NormRoot)
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: refusing — destination '%s' is not a direct child of the "
					"export root '%s' (export key '%s'). Nothing exported."),
					*NormOut,
					*NormRoot,
					*SelfKey);
				return;
			}
		}
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ExportQuestline: destination '%s'."), *OutDir);

		const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args, TEXT("ExportQuestline"));
		if (!Format)
		{
			return;   // the unregistered-format error was already logged; nothing exported.
		}
		// OWNERSHIP — never replace a folder we didn't write. This is the guard that survives a NAME COLLISION: two
		// questline IDs can sanitize to one folder, and a hand-authored source folder sitting at that name would otherwise
		// be overwritten by an export.
		FQuestExportMarker Previous;
		const bool bHadMarker = ReadQuestExportMarker(OutDir, Previous);
		const TArray<FString> Existing = QuestExportFilesIn(OutDir);
		if (Existing.Num() > 0 && !bHadMarker)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: refusing — '%s' already holds %d file(s) and carries no "
				"SimpleQuest export marker, so an export did not write it. Exporting would replace its contents. Move or delete "
				"that folder, or give this questline a different QuestlineID. Nothing written."),
				*OutDir,
				Existing.Num());
			return;
		}
		if (bHadMarker && !Previous.SourceAsset.IsEmpty() && Previous.SourceAsset != Args[0])
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: refusing — '%s' holds the export of a DIFFERENT questline "
				"('%s'). Their IDs reduce to the same folder name, so each would overwrite the other. Give one a distinct "
				"QuestlineID. Nothing written."),
				*OutDir,
				*Previous.SourceAsset);
			return;
		}

		// STAGE — write the complete new export beside the destination. NOTHING is deleted until it exists on disk, so a
		// failed, refused or interrupted write leaves the previous export exactly as it was. The sanitizer can never emit a
		// '.', so this name cannot collide with a real destination.
		const FString Staging = OutDir + TEXT(".incoming");
		IFileManager::Get().DeleteDirectory(*Staging, /*RequireExists*/ false, /*Tree*/ true);
		if (!Format->WriteBundle(Bundle, Staging))
		{
			IFileManager::Get().DeleteDirectory(*Staging, false, true);
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: the %s provider failed to write. '%s' is unchanged."),
				*Format->FormatName(),
				*OutDir);
			return;
		}

		FQuestExportMarker Marker;
		Marker.Format = Format->FormatName();
		Marker.SourceAsset = Args[0];
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
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: couldn't remove '%s' from the previous export — it may be "
					"read-only or open elsewhere. '%s' is unchanged; the finished new export is at '%s'."),
					*OldPath,
					*OutDir,
					*Staging);
				return;
			}
			++Removed;
		}
		if (bHadMarker) { IFileManager::Get().Delete(*(OutDir / GQuestExportMarkerName), false, false, false); }

		WriteQuestExportMarker(Staging, Marker);
		for (const FString& New : Marker.Files)
		{
			if (!IFileManager::Get().Move(*(OutDir / New), *(Staging / New)))
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: couldn't move '%s' into place — '%s' is now PARTIAL and "
					"should not be imported. The complete export is at '%s'."),
					*New,
					*OutDir,
					*Staging);
				return;
			}
		}
		IFileManager::Get().Move(*(OutDir / GQuestExportMarkerName), *(Staging / GQuestExportMarkerName));
		IFileManager::Get().DeleteDirectory(*Staging, false, true);   // scratch only; a failure here is not data loss

		int32 RowTotal = 0;
		for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
		{
			RowTotal += TablePair.Value.Rows.Num();
		}
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ExportQuestline: '%s' — %d entity row(s) across %d type(s), %d edge(s), %d knot(s) "
			"collapsed. Wrote %d file(s) to '%s'; removed %d from the previous export."),
			*SelfKey,
			RowTotal,
			Bundle.TablesByType.Num(),
			Bundle.Edges.Num(),
			Bundle.KnotsCollapsed,
			Marker.Files.Num(),
			*OutDir,
			Removed);
	}
}

static FAutoConsoleCommand GExportQuestlineCmd(
	TEXT("SimpleQuest.ExportQuestline"),
	TEXT("PROTOTYPE: export a questline's authored model as the interlingua folder — per-type entity tables "
		"(reflection-driven, instanced sub-objects as child rows) + one knot-collapsed edge table — to "
		"Saved/QuestExport/<QuestlineID>/. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportQuestlineCmd));
