// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


// PROTOTYPE — Resolver, Phase 2 round-trip harness. One command that runs the full oracle loop on a questline asset:
// export -> import (_RT) -> dump both -> report the C diff (authored folders) and the B2 diff (compiled dumps), each
// with the ONE known-benign normalization applied (the _RT identity suffix for C, the tag namespace prefix for B2).
// The normalization is the point — a raw diff false-fails on every asset. Drives the three existing console commands
// via GEngine->Exec (same-process), so it needs no refactoring of the export/import/dump prototypes. Editor-only,
// console-triggered. Not shipped API.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/Verification/QuestRoundTripOracle.h"
#include "Resolver/Verification/QuestVerificationPaths.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	void RoundTripCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("RoundTrip: usage 'SimpleQuest.RoundTrip <QuestlineAssetPath> <DestPackagePath>'."));
			return;
		}
		const FString AssetPath = Args[0];
		const FString DestPackagePath = Args[1];

		// Forward an optional "--format=<name>" to the export/import sub-commands so the whole round-trip uses the chosen
		// provider (default TSV). Without this the harness would silently run TSV even when --format=json was requested.
		// DumpCompiled needs no format (it reads the compiled asset, not a file). NOTE: Args[0]/[1] are positional; a
		// --format arg would be Args[2], so it doesn't disturb the positional reads above.
		FString FormatArg;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--format="))) { FormatArg = FString(TEXT(" ")) + Arg; break; }
		}

		const UQuestlineGraph* Src = LoadObject<UQuestlineGraph>(nullptr, *AssetPath);
		if (!Src) { UE_LOG(LogSimpleQuestResolver, Error, TEXT("RoundTrip: couldn't load '%s'."), *AssetPath); return; }
		const FString OriginalID = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Src->GetEffectiveID());

		const FString ExportRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport"));
		const FString SrcFolder  = ExportRoot / OriginalID;
		const FString RtFolder   = ExportRoot / (OriginalID + GQuestRoundTripSuffix);
		const FString SrcDump    = ExportRoot / (OriginalID + TEXT("_compiled_dump.tsv"));
		const FString RtDump     = ExportRoot / (OriginalID + GQuestRoundTripSuffix + TEXT("_compiled_dump.tsv"));

		auto Exec = [](const FString& Cmd)
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("RoundTrip: > %s"), *Cmd);
			GEngine->Exec(nullptr, *Cmd);
		};

		// 1. Export the source (authored folder + we'll dump its compiled form too).
		Exec(FString::Printf(TEXT("SimpleQuest.ExportQuestline %s%s"), *AssetPath, *FormatArg));
		Exec(FString::Printf(TEXT("SimpleQuest.DumpCompiled %s"), *AssetPath));

		// 2. Import from the source folder -> creates <ID>_RT in DestPackagePath.
		Exec(FString::Printf(TEXT("SimpleQuest.ImportQuestline %s %s%s"), *SrcFolder, *DestPackagePath, *FormatArg));

		// 3. Export + dump the imported asset. Its object path: <DestPackagePath>/<ID>_RT.<ID>_RT
		const FString RtAssetPath = FString::Printf(TEXT("%s/%s%s.%s%s"),
			*DestPackagePath, *OriginalID, GQuestRoundTripSuffix, *OriginalID, GQuestRoundTripSuffix);
		Exec(FString::Printf(TEXT("SimpleQuest.ExportQuestline %s%s"), *RtAssetPath, *FormatArg));
		Exec(FString::Printf(TEXT("SimpleQuest.DumpCompiled %s"), *RtAssetPath));

		// 4. Compare, normalized.
		const int32 CMiss  = CompareQuestExportFolders(SrcFolder, RtFolder, OriginalID);
		const int32 B2Miss = CompareQuestCompiledDumps(SrcDump, RtDump, OriginalID);

		UE_LOG(LogSimpleQuestResolver, Log, TEXT("==== RoundTrip '%s': C %s (%d), B2 %s (%d) ===="),
			*OriginalID,
			CMiss  == 0 ? TEXT("PASS") : TEXT("FAIL"), CMiss,
			B2Miss == 0 ? TEXT("PASS") : TEXT("FAIL"), B2Miss);
	}

	// Compare-only: run the C + B2 comparators against artifacts ALREADY on disk, without re-export/re-import. This is
	// the harness's own smoke-test seam — regenerate ONLY the RT-side artifacts against a deliberately-corrupted _RT
	// asset (ExportQuestline + DumpCompiled on it), leave the src-side artifacts pristine from the last full RoundTrip,
	// then call this to confirm the REAL comparators (not a re-implementation) go red on the injected break. Because a
	// full RoundTrip re-imports and self-heals, corruption can only be observed through this no-regen compare path.
	// Args: <OriginalID> — the sanitized questline ID whose <ID>/<ID>_RT folders + <ID>_compiled_dump.tsv /
	// <ID>_RT_compiled_dump.tsv dumps live under Saved/QuestExport (i.e. the same stems a prior RoundTrip wrote).
	void RoundTripCompareCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("RoundTripCompare: usage 'SimpleQuest.RoundTripCompare <OriginalID>' "
				"(compares the on-disk <ID> vs <ID>_RT artifacts a prior RoundTrip left; no re-export/import)."));
			return;
		}
		const FString OriginalID = Args[0];
		const FString ExportRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport"));
		const FString SrcFolder  = ExportRoot / OriginalID;
		const FString RtFolder   = ExportRoot / (OriginalID + GQuestRoundTripSuffix);
		const FString SrcDump    = ExportRoot / (OriginalID + TEXT("_compiled_dump.tsv"));
		const FString RtDump     = ExportRoot / (OriginalID + GQuestRoundTripSuffix + TEXT("_compiled_dump.tsv"));

		const int32 CMiss  = CompareQuestExportFolders(SrcFolder, RtFolder, OriginalID);
		const int32 B2Miss = CompareQuestCompiledDumps(SrcDump, RtDump, OriginalID);
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("==== RoundTripCompare '%s': C %s (%d), B2 %s (%d) ===="),
			*OriginalID,
			CMiss  == 0 ? TEXT("PASS") : TEXT("FAIL"), CMiss,
			B2Miss == 0 ? TEXT("PASS") : TEXT("FAIL"), B2Miss);
	}
}

static FAutoConsoleCommand GRoundTripCmd(
	TEXT("SimpleQuest.RoundTrip"),
	TEXT("PROTOTYPE: full oracle loop on a questline — export, import (_RT), dump both, report the C (authored folder) "
		"and B2 (compiled dump) diffs, each _RT-normalized. Args: <QuestlineAssetPath> <DestPackagePath>."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&RoundTripCmd));

static FAutoConsoleCommand GRoundTripCompareCmd(
	TEXT("SimpleQuest.RoundTripCompare"),
	TEXT("PROTOTYPE / smoke-test: run the C + B2 comparators against the <ID> vs <ID>_RT artifacts a prior RoundTrip "
		"left on disk, WITHOUT re-export/import. Corrupt the _RT asset, re-run ExportQuestline+DumpCompiled on it, then "
		"this — the real comparators should go red on the injected break. Args: <OriginalID>."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&RoundTripCompareCmd));
