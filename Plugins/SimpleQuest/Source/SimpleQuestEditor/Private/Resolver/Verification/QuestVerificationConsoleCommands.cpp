// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// The verification tier's console surface: dump a compiled model, run the full round-trip loop, or re-run just the
// comparators against artifacts already on disk. All three are read-only with respect to the questlines they judge -
// RoundTrip writes a reconstructed copy, but only ever under the destination package the caller names.

#include "CoreMinimal.h"
#include "ISimpleQuestEditorModule.h"
#include "ObjectTools.h"
#include "Engine/Engine.h"
#include "Graph/QuestGraphArrange.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestPlanBroker.h"
#include "Resolver/Verification/QuestCompiledModelDump.h"
#include "Resolver/Verification/QuestRoundTripOracle.h"
#include "Resolver/Verification/QuestVerificationPaths.h"
#include "SimpleQuestLog.h"
#include "UObject/UObjectGlobals.h"
#include "Utilities/QuestlineGraphCompiler.h"
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
		// provider (else the project default). Without this the harness would silently run the default even when a format
		// was requested. DumpCompiled needs no format (it reads the compiled asset, not a file). NOTE: Args[0]/[1] are
		// positional; a --format arg would be Args[2], so it doesn't disturb the positional reads above.
		FString FormatArg;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--format="))) { FormatArg = FString(TEXT(" ")) + Arg; break; }
		}
		// The authored comparator reads whatever files the provider wrote, so resolve the provider the same way the
		// sub-commands will - one resolution, and all three agree on the format.
		const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args, TEXT("RoundTrip"));
		if (!Format) return;
		const FString FileExtension = Format->FileExtension();

		UQuestlineGraph* Src = LoadObject<UQuestlineGraph>(nullptr, *AssetPath);
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

		// RECEIPTS, NOT FORENSICS. bEveryStepRan above catches a command that does not EXIST; it cannot catch one that
		// runs and REFUSES, and a refused export leaves the previous run's folder looking exactly like a successful one.
		// This used to be answered by stamping FDateTime::UtcNow() and requiring every artifact to be newer - which is a
		// race, and one that a FAST run loses: IFileManager::GetTimeStamp truncates a file's timestamp to the WHOLE
		// SECOND on every platform (WindowsFileTimeToUEDateTime drops the milliseconds deliberately; Apple and Unix read
		// st_mtime, which has none), while UtcNow() carries milliseconds. A run that finishes inside one second reads its
		// own fresh artifacts as older than its start and aborts. Reported from the field on 5.8; it had always been true.
		// So stop asking the disk what happened and let the pipeline say: the export publishes a receipt on success and
		// its refusal text on failure, through the same broker an open panel listens to.
		TMap<FString, FString> ExportReceiptByAsset;   // canonical asset path -> receipt; only successes land here
		TMap<FString, FString> ExportRefusalByAsset;   // canonical asset path -> why it refused
		const FDelegateHandle ExportHandle = FQuestPlanBroker::Get().OnExportCompleted().AddLambda(
			[&ExportReceiptByAsset, &ExportRefusalByAsset](const FString& Asset, const FString& Summary, const FString& Error)
			{
				if (Error.IsEmpty()) { ExportReceiptByAsset.Add(Asset, Summary); }
				else                 { ExportRefusalByAsset.Add(Asset, Error); }
			});
		ON_SCOPE_EXIT { FQuestPlanBroker::Get().OnExportCompleted().Remove(ExportHandle); };

		// Keyed by the asset's OWN path name, not by the console argument - a caller may have typed either the short or
		// the object-path form, and the broker reports what the graph calls itself.
		const FString SrcKey = Src->GetPathName();

		// COMPILE THE SOURCE BEFORE DUMPING IT. ImportQuestline compiles the RT side moments before its dump, so
		// leaving the source at whatever state it was last saved in compares a FRESH artifact against a STALE one -
		// and the verdict then reports when someone last pressed Compile rather than what the pipeline did. It can
		// fail either way: a stale source that is missing wiring reads as the import inventing it, and a stale source
		// that still holds since-deleted wiring reads as a clean pass.
		// Through the module's factory rather than by constructing one, so an adopter's replacement compiler is the
		// one under test - verifying the default while shipping a substitute would be verifying the wrong thing.
		// The toolkit's ceremony (message-log pages, linked-neighbour compiles, rename capture) is deliberately not
		// mirrored: none of it reaches the compiled model this harness reads.
		{
			TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
			if (!Compiler->Compile(Src))
			{
				// ABORT WITH NO VERDICT, matching the missing-command check below. A source with compile ERRORS has no
				// trustworthy compiled model, so anything the comparison says about it is unearned in either direction.
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("RoundTrip: the source '%s' does not compile cleanly. "
					"Fix the compile errors first - a round trip against a broken source proves nothing."), *AssetPath);
				return;
			}
		}

		// The two compiled dumps are this harness's own artifacts and this run rewrites both, so clearing them first
		// turns "written by this run" into a question about EXISTENCE, which no clock can get wrong. ONLY these two
		// NAMED FILES. The export FOLDERS are never touched: one can hold a hand-placed marker whose bOwned is false, a
		// bundle somebody edited and has not imported yet, or read-only files the export itself declines to remove -
		// and a verification harness must not be more destructive than the pipeline it verifies. EvenReadOnly is FALSE
		// for that last reason, which is the choice the export's own replace step makes.
		// BELOW THE COMPILE GATE ON PURPOSE: that gate returns, and deleting artifacts we are then not going to
		// regenerate is the failure this whole rewrite exists to avoid.
		IFileManager::Get().Delete(*SrcDump, false, false, true);
		IFileManager::Get().Delete(*RtDump,  false, false, true);

		// 1. Export the source (authored folder + we'll dump its compiled form too).
		Exec(FString::Printf(TEXT("SimpleQuest.ExportQuestline %s%s"), *AssetPath, *FormatArg));
		Exec(FString::Printf(TEXT("SimpleQuest.DumpCompiled %s"), *AssetPath));

		// RoundTrip OWNS the <ID>_RT name, so a leftover from an earlier run is this command's mess to clear rather than
		// a collision to report. Left in place it keeps claiming the previous run's QuestlineID, and two assets in one
		// tag namespace is a compile collision with nothing to do with what is being verified - it reads as a pipeline
		// fault and is not one.
		{
			const FString ExistingPath = FString::Printf(TEXT("%s/%s.%s"), *DestPackagePath, *RtID, *RtID);
			if (UObject* Leftover = StaticLoadObject(UQuestlineGraph::StaticClass(), nullptr, *ExistingPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				const int32 Deleted = ObjectTools::ForceDeleteObjects({ Leftover }, /*ShowConfirmation*/ false);
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("RoundTrip: cleared %d leftover '%s' asset(s) from an earlier run."),
					   Deleted, *RtID);
			}
		}

		// 2. Import from the source folder -> creates <ID>_RT in DestPackagePath.
		Exec(FString::Printf(TEXT("SimpleQuest.ImportQuestline %s %s%s"), *SrcFolder, *DestPackagePath, *FormatArg));

		// 3. Export + dump the imported asset. Its object path: <DestPackagePath>/<ID>_RT.<ID>_RT
		const FString RtAssetPath = FString::Printf(TEXT("%s/%s.%s"), *DestPackagePath, *RtID, *RtID);

		// Loading it here does double duty: the import is the one step with no receipt of its own, so a null result IS
		// the import's failure report - and a loaded asset gives us the canonical path the broker will key its receipt by.
		const UQuestlineGraph* Rt = LoadObject<UQuestlineGraph>(nullptr, *RtAssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		const FString RtKey = Rt ? Rt->GetPathName() : FString();

		// Answer the question NOW rather than holding a raw pointer across the two Exec calls below. Nothing here
		// dereferences Rt afterwards, but a collected asset would leave it dangling and non-null, and the verdict gate
		// should not be one edit away from reading a field off it.
		const bool bImportProducedAsset = (Rt != nullptr);

		Exec(FString::Printf(TEXT("SimpleQuest.ExportQuestline %s%s"), *RtAssetPath, *FormatArg));
		Exec(FString::Printf(TEXT("SimpleQuest.DumpCompiled %s"), *RtAssetPath));

		// For the log line only - never for a verdict. What is on disk answers "is something here", and the receipts
		// above answer "did this run put it here", which is the question that actually matters.
		auto FileCountIn = [](const FString& Dir)
		{
			TArray<FString> Found;
			IFileManager::Get().FindFiles(Found, *(Dir / TEXT("*.*")), /*Files*/ true, /*Directories*/ false);
			return Found.Num();
		};

		// The evidence the last field report had to reconstruct by hand. Verbose, because it is worth nothing on a run
		// that passes and is the whole answer on a run that does not.
		UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("RoundTrip '%s': src folder '%s' %d file(s), src dump %s; rt folder "
			"'%s' %d file(s), rt dump %s; %d export receipt(s), %d refusal(s)."),
			*OriginalID,
			*SrcFolder, FileCountIn(SrcFolder), IFileManager::Get().FileExists(*SrcDump) ? TEXT("present") : TEXT("MISSING"),
			*RtFolder,  FileCountIn(RtFolder),  IFileManager::Get().FileExists(*RtDump)  ? TEXT("present") : TEXT("MISSING"),
			ExportReceiptByAsset.Num(), ExportRefusalByAsset.Num());

		// Ordered the way the run is, so the FIRST thing that went wrong is the thing reported - a later check failing
		// because of an earlier failure is noise. A refusal now carries its own reason, so "check the errors above" is
		// no longer the only thing this can say.

		// The pipeline's refusals are complete sentences and end in a period; this line adds its own. Trim one, so a
		// quoted refusal does not read "... Nothing written.. No verdict."
		auto Unterminated = [](const FString& Text)
		{
			FString Trimmed = Text.TrimEnd();
			Trimmed.RemoveFromEnd(TEXT("."));
			return Trimmed;
		};

		FString AbortWhy;
		if (!bEveryStepRan)
		{
			AbortWhy = TEXT("a step was skipped entirely");
		}
		else if (const FString* SrcRefusal = ExportRefusalByAsset.Find(SrcKey))
		{
			AbortWhy = FString::Printf(TEXT("the source export refused - %s"), *Unterminated(*SrcRefusal));
		}
		else if (!ExportReceiptByAsset.Contains(SrcKey))
		{
			AbortWhy = TEXT("the source export reported neither a result nor a refusal");
		}
		else if (!IFileManager::Get().FileExists(*SrcDump))
		{
			AbortWhy = TEXT("the source compiled dump was not written");
		}
		else if (!bImportProducedAsset)
		{
			AbortWhy = FString::Printf(TEXT("the import produced no '%s' asset"), *RtID);
		}
		else if (const FString* RtRefusal = ExportRefusalByAsset.Find(RtKey))
		{
			AbortWhy = FString::Printf(TEXT("the round-trip export refused - %s"), *Unterminated(*RtRefusal));
		}
		else if (!ExportReceiptByAsset.Contains(RtKey))
		{
			AbortWhy = TEXT("the round-trip export reported neither a result nor a refusal");
		}
		else if (!IFileManager::Get().FileExists(*RtDump))
		{
			AbortWhy = TEXT("the round-trip compiled dump was not written");
		}

		if (!AbortWhy.IsEmpty())
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("==== RoundTrip '%s': ABORTED - %s. No verdict. ===="),
				*OriginalID, *AbortWhy);
			return;
		}

		// 4. Compare, normalized.
		const int32 AuthoredMiss = CompareQuestExportFolders(SrcFolder, RtFolder, OriginalID, FileExtension);
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
	// Args: <OriginalID> [--format=<name>] — the sanitized questline ID whose <ID>/<ID>_RT folders + <ID>_compiled_dump.tsv /
	// <ID>_RT_compiled_dump.tsv dumps live under Saved/QuestExport (i.e. the same stems a prior RoundTrip wrote). The
	// format names which files the folders hold, resolved exactly as RoundTrip resolved it when it wrote them.
	void RoundTripCompareCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("RoundTripCompare: usage 'SimpleQuest.RoundTripCompare <OriginalID> [--format=<name>]' "
				"(compares the on-disk <ID> vs <ID>_RT artifacts a prior RoundTrip left; no re-export/import)."));
			return;
		}
		const FString OriginalID = Args[0];
		const FString RtID       = OriginalID + GQuestRoundTripSuffix;

		// The two command names invite this: RoundTrip takes an ASSET PATH, RoundTripCompare takes the questline ID whose
		// artifacts a prior RoundTrip left on disk. Passing a path used to resolve to folders that never existed and
		// report a diff against nothing, which reads as a pipeline fault and is not one.
		if (OriginalID.Contains(TEXT("/")) || OriginalID.Contains(TEXT(".")))
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("RoundTripCompare: '%s' looks like an asset path. This command takes "
				"the questline ID whose <ID> / <ID>_RT artifacts live under Saved/QuestExport - try 'SimpleQuest."
				"RoundTripCompare %s'. Nothing compared."), *OriginalID, *FPaths::GetBaseFilename(OriginalID));
			return;
		}

		const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args, TEXT("RoundTripCompare"));
		if (!Format) return;

		const int32 AuthoredMiss = CompareQuestExportFolders(QuestExport_FolderForKey(OriginalID), QuestExport_FolderForKey(RtID), OriginalID, Format->FileExtension());
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
		"the cycle intact reports 0 for both. Args: <QuestlineAssetPath> <DestPackagePath> [--format=<name>]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&RoundTripCmd));

static FAutoConsoleCommand GRoundTripCompareCmd(
	TEXT("SimpleQuest.RoundTripCompare"),
	TEXT("Smoke-test seam: run both comparators against the <ID> vs <ID>_RT artifacts a prior RoundTrip left on disk, "
		"WITHOUT re-export/import. Corrupt the _RT asset, re-run ExportQuestline+DumpCompiled on it, then this — the "
		"real comparators should go red on the injected break. Args: <OriginalID> [--format=<name>]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&RoundTripCompareCmd));

static FAutoConsoleCommand GLogGraphRanksCmd(
	TEXT("SimpleQuest.LogGraphRanks"),
	TEXT("Log the layered rank of every node in a questline's root graph. Computes only - moves nothing."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&LogGraphRanksCmd));

static FAutoConsoleCommand GArrangeGraphCmd(
	TEXT("SimpleQuest.ArrangeGraph"),
	TEXT("Lay out a questline's graph left-to-right by rank, including every container's inner graph. Undoable."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ArrangeGraphCmd));

