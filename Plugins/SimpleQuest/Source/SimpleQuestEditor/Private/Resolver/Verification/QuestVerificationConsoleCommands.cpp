// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// The verification tier's console surface: dump a compiled model, run the full round-trip loop, or re-run just the
// comparators against artifacts already on disk. All three are read-only with respect to the questlines they judge -
// RoundTrip writes a reconstructed copy, but only ever under the destination package the caller names.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/Verification/QuestCompiledModelDump.h"
#include "Resolver/Verification/QuestRoundTripOracle.h"
#include "Resolver/Verification/QuestVerificationPaths.h"
#include "SimpleQuestLog.h"
#include "Graph/QuestGraphArrange.h"
#include "UObject/UObjectGlobals.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	void DumpCompiledCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("DumpCompiled: usage 'SimpleQuest.DumpCompiled <QuestlineAssetPath>'."));
			return;
		}
		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("DumpCompiled: couldn't load questline asset '%s'."), *Args[0]);
			return;
		}

		int32 NodeCount = 0;
		const TArray<FString> Lines = RenderQuestCompiledModel(*Graph, NodeCount);

		IFileManager::Get().MakeDirectory(*QuestExport_RootDir(), true);
		const FString QLID = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Graph->GetEffectiveID());
		const FString OutPath = QuestCompiledDumpPathFor(QLID);
		if (FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutPath))
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("DumpCompiled: '%s' — %d line(s), %d node(s). Wrote '%s'."),
				*QLID, Lines.Num(), NodeCount, *OutPath);
		}
		else
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("DumpCompiled: failed to write '%s'."), *OutPath);
		}
	}

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

		const FString RtID			= OriginalID + GQuestRoundTripSuffix;
		const FString SrcFolder		= QuestExport_FolderForKey(OriginalID);
		const FString RtFolder		= QuestExport_FolderForKey(RtID);
		const FString SrcDump		= QuestCompiledDumpPathFor(OriginalID);
		const FString RtDump		= QuestCompiledDumpPathFor(RtID);

		// This harness drives itself through the console, and GEngine->Exec returns WITHOUT COMPLAINT for a command
		// that is not registered - so dropping a registration is indistinguishable from a clean run, and every green
		// result this harness has ever produced was weaker evidence than it looked. Checked before dispatch rather
		// than inferred from Exec's return, which answers "was this handled" rather than "does this exist".
		bool bEveryStepRan = true;
		auto Exec = [&bEveryStepRan](const FString& Cmd)
		{
			FString Name = Cmd;
			Cmd.Split(TEXT(" "), &Name, nullptr);   // leaves Name as the whole string when there are no arguments
			if (!IConsoleManager::Get().FindConsoleObject(*Name))
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("RoundTrip: '%s' is not a registered console command - the step "
					"was skipped."), *Name);
				bEveryStepRan = false;
				return;
			}
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("RoundTrip: > %s"), *Cmd);
			GEngine->Exec(nullptr, *Cmd);
		};

		// 1. Export the source (authored folder + we'll dump its compiled form too).
		Exec(FString::Printf(TEXT("SimpleQuest.ExportQuestline %s%s"), *AssetPath, *FormatArg));
		Exec(FString::Printf(TEXT("SimpleQuest.DumpCompiled %s"), *AssetPath));

		// 2. Import from the source folder -> creates <ID>_RT in DestPackagePath.
		Exec(FString::Printf(TEXT("SimpleQuest.ImportQuestline %s %s%s"), *SrcFolder, *DestPackagePath, *FormatArg));

		// 3. Export + dump the imported asset. Its object path: <DestPackagePath>/<ID>_RT.<ID>_RT
		const FString RtAssetPath = FString::Printf(TEXT("%s/%s.%s"), *DestPackagePath, *RtID, *RtID);
		Exec(FString::Printf(TEXT("SimpleQuest.ExportQuestline %s%s"), *RtAssetPath, *FormatArg));
		Exec(FString::Printf(TEXT("SimpleQuest.DumpCompiled %s"), *RtAssetPath));

		// A step that did not run leaves the PREVIOUS run's artifacts on disk, and those compare clean - so a skipped
		// step does not merely weaken the verdict, it manufactures a passing one. Refuse to report rather than report
		// something unearned.
		if (!bEveryStepRan)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("==== RoundTrip '%s': ABORTED - at least one step did not run, so "
				"any artifacts on disk are from an earlier run. No verdict. ===="), *OriginalID);
			return;
		}

		// 4. Compare, normalized.
		const int32 AuthoredMiss = CompareQuestExportFolders(SrcFolder, RtFolder, OriginalID);
		const int32 CompiledMiss = CompareQuestCompiledDumps(SrcDump, RtDump, OriginalID);

		UE_LOG(LogSimpleQuestResolver, Log, TEXT("==== RoundTrip '%s': authored %s (%d), compiled %s (%d) ===="),
			*OriginalID,
			AuthoredMiss == 0 ? TEXT("PASS") : TEXT("FAIL"), AuthoredMiss,
			CompiledMiss == 0 ? TEXT("PASS") : TEXT("FAIL"), CompiledMiss);
	}

	// Compare-only: run the comparators against artifacts ALREADY on disk, without re-export/re-import. This is the
	// harness's own smoke-test seam — regenerate ONLY the RT-side artifacts against a deliberately-corrupted _RT asset
	// (ExportQuestline + DumpCompiled on it), leave the src-side artifacts pristine from the last full RoundTrip, then
	// call this to confirm the REAL comparators (not a re-implementation) go red on the injected break. Because a full
	// RoundTrip re-imports and self-heals, corruption can only be observed through this no-regen compare path.
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
		const FString RtID       = OriginalID + GQuestRoundTripSuffix;

		const int32 AuthoredMiss = CompareQuestExportFolders(QuestExport_FolderForKey(OriginalID), QuestExport_FolderForKey(RtID), OriginalID);
		const int32 CompiledMiss = CompareQuestCompiledDumps(QuestCompiledDumpPathFor(OriginalID), QuestCompiledDumpPathFor(RtID), OriginalID);
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("==== RoundTripCompare '%s': authored %s (%d), compiled %s (%d) ===="),
			*OriginalID,
			AuthoredMiss == 0 ? TEXT("PASS") : TEXT("FAIL"), AuthoredMiss,
			CompiledMiss == 0 ? TEXT("PASS") : TEXT("FAIL"), CompiledMiss);
	}
	static void LogGraphRanksCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("LogGraphRanks: usage 'SimpleQuest.LogGraphRanks <QuestlineAssetPath>'."));
			return;
		}
		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("LogGraphRanks: couldn't load '%s'."), *Args[0]);
			return;
		}

		TArray<FQuestNodeRank> Ranks;
		RankQuestGraphNodes(*Graph->QuestlineEdGraph, Ranks);

		UE_LOG(LogSimpleQuestResolver, Log, TEXT("LogGraphRanks: '%s' - %d ranked node(s)."), *Args[0], Ranks.Num());
		for (const FQuestNodeRank& R : Ranks)
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("  rank %2d  %-40s  %s"),
				R.Rank,
				*R.Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				*R.OrderKey);
		}
	}

	static void ArrangeGraphCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ArrangeGraph: usage 'SimpleQuest.ArrangeGraph <QuestlineAssetPath>'."));
			return;
		}
		UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ArrangeGraph: couldn't load '%s'."), *Args[0]);
			return;
		}

		FScopedTransaction Transaction(NSLOCTEXT("SimpleQuest", "ArrangeGraph", "Arrange questline graph"));
		const int32 Moved = ArrangeQuestGraph(*Graph->QuestlineEdGraph, /*bRecurseIntoContainers*/ true);
		if (Moved > 0) { Graph->MarkPackageDirty(); }

		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ArrangeGraph: '%s' - %d node(s) moved."), *Args[0], Moved);
	}
}

static FAutoConsoleCommand GDumpCompiledCmd(
	TEXT("SimpleQuest.DumpCompiled"),
	TEXT("Dump a questline's COMPILED model as deterministic text (per-node reflection dump, sets/maps sorted, prereq "
		"combinator children order-normalized) to Saved/QuestExport/<QuestlineID>_compiled_dump.tsv. Two dumps diff "
		"clean iff the questlines compile to identical behaviour. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCompiledCmd));

static FAutoConsoleCommand GRoundTripCmd(
	TEXT("SimpleQuest.RoundTrip"),
	TEXT("Full round-trip check on a questline — export, import (_RT), dump both, then report two diffs: AUTHORED "
		"(the export folders) and COMPILED (the compiled-model dumps), each _RT-normalized. A questline that survives "
		"the cycle intact reports 0 for both. Args: <QuestlineAssetPath> <DestPackagePath>."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&RoundTripCmd));

static FAutoConsoleCommand GRoundTripCompareCmd(
	TEXT("SimpleQuest.RoundTripCompare"),
	TEXT("Smoke-test seam: run both comparators against the <ID> vs <ID>_RT artifacts a prior RoundTrip left on disk, "
		"WITHOUT re-export/import. Corrupt the _RT asset, re-run ExportQuestline+DumpCompiled on it, then this — the "
		"real comparators should go red on the injected break. Args: <OriginalID>."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&RoundTripCompareCmd));

static FAutoConsoleCommand GLogGraphRanksCmd(
	TEXT("SimpleQuest.LogGraphRanks"),
	TEXT("Log the layered rank of every node in a questline's root graph. Computes only - moves nothing."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&LogGraphRanksCmd));

static FAutoConsoleCommand GArrangeGraphCmd(
	TEXT("SimpleQuest.ArrangeGraph"),
	TEXT("Lay out a questline's graph left-to-right by rank, including every container's inner graph. Undoable."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ArrangeGraphCmd));

