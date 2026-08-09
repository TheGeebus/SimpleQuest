// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


// PROTOTYPE — Resolver, Phase 2 oracle instrument. Dumps a questline's COMPILED model (everything the compiler
// stamps) as deterministic text: per-node reflection dump with container-aware sorting (sets and map keys sorted;
// prereq combinator children order-normalized — AND/OR are commutative, so child order is authoring noise), plus
// the graph-level compiled containers. Two dumps diff clean iff the graphs compile to identical behavior — the
// import round-trip's behavioral judge (B2). Also the compiled model's first inspection surface outside the editor
// panels. Read-only, console-triggered. Not shipped API.

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/Verification/QuestCompiledModelDump.h"
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

		const FString OutDir = FPaths::ProjectSavedDir() / TEXT("QuestExport");
		IFileManager::Get().MakeDirectory(*OutDir, true);
		const FString QLID = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Graph->GetEffectiveID());
		const FString OutPath = FPaths::ConvertRelativePathToFull(OutDir / (QLID + TEXT("_compiled_dump.tsv")));
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
}

static FAutoConsoleCommand GDumpCompiledCmd(
	TEXT("SimpleQuest.DumpCompiled"),
	TEXT("PROTOTYPE: dump a questline's COMPILED model as deterministic text (per-node reflection dump, sets/maps "
		"sorted, prereq combinator children order-normalized) to Saved/QuestExport/<QuestlineID>_compiled_dump.tsv. "
		"The import round-trip's behavioral judge. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCompiledCmd));