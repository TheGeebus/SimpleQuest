// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Commandlets/StaleQuestTagsScanCommandlet.h"

#include "SimpleQuestLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Utilities/SimpleQuestEditorUtils.h"

#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* SourceText(FSimpleQuestEditorUtilities::EStaleQuestTagSource Source)
	{
		using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;
		switch (Source)
		{
			case EStaleQuestTagSource::LoadedLevelInstance:   return TEXT("Open");
			case EStaleQuestTagSource::ActorBlueprintCDO:     return TEXT("BPCDO");
			case EStaleQuestTagSource::UnloadedLevelInstance: return TEXT("Unloaded");
			default:                                          return TEXT("Unknown");
		}
	}
}

UStaleQuestTagsScanCommandlet::UStaleQuestTagsScanCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;     // GEditor must be available — we use editor world contexts, AR queries, and sync-load
	// BPs/umaps. Without this, UE 5.6's TedsCore plugin (loaded by default) asserts on
	// GEditor during the engine-startup-modules-loaded callback before Main() even runs.
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UStaleQuestTagsScanCommandlet::Main(const FString& Params)
{
	// Argument parsing
	FString OutputJsonPath;
	FParse::Value(*Params, TEXT("OutputJson="), OutputJsonPath);

	const bool bFastWP = FParse::Param(*Params, TEXT("FastWP"));

	// Commandlet-mode Asset Registry priming. In editor mode the AR auto-scans during startup; in
	// commandlet mode it doesn't, and IsLoadingAssets() returns false even though AR is empty (it's
	// not "still loading" — it just hasn't been told to start). Without this, every AR query in our
	// scan helpers returns zero, and the scan reports 0 stale references regardless of project state.
	// SearchAllAssets is idempotent and cheap on an already-populated AR (editor mode no-op).
	{
		UE_LOG(LogSimpleQuest, Display, TEXT("StaleQuestTagsScan: priming Asset Registry (synchronous full scan)..."));
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const double ScanStartTime = FPlatformTime::Seconds();
		ARM.Get().SearchAllAssets(/*bSynchronousSearch*/ true);
		const double ScanElapsed = FPlatformTime::Seconds() - ScanStartTime;
		UE_LOG(LogSimpleQuest, Display, TEXT("StaleQuestTagsScan: Asset Registry priming completed in %.2fs"), ScanElapsed);
	}

	// Resolve relative JSON paths against the project dir.
	if (!OutputJsonPath.IsEmpty() && FPaths::IsRelative(OutputJsonPath))
	{
		OutputJsonPath = FPaths::Combine(FPaths::ProjectDir(), OutputJsonPath);
	}

	// Scan with all surfaces enabled.
	FSimpleQuestEditorUtilities::FStaleTagScanScope Scope;
	Scope.bLoadedLevels        = true;   // commandlet typically has no editor worlds; harmless empty walk
	Scope.bActorBlueprintCDOs  = true;
	Scope.bUnloadedLevels      = true;
	Scope.bComprehensiveWPScan = !bFastWP;

	UE_LOG(LogSimpleQuest, Display,
		TEXT("StaleQuestTagsScan: starting (WP mode=%s, JSON output=%s)"),
		bFastWP ? TEXT("class-filtered") : TEXT("comprehensive"),
		OutputJsonPath.IsEmpty() ? TEXT("(none)") : *OutputJsonPath);

	const TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry> Entries =
		FSimpleQuestEditorUtilities::CollectStaleQuestTagEntries(Scope);

	// Tally per source.
	using EStaleQuestTagSource = FSimpleQuestEditorUtilities::EStaleQuestTagSource;
	int32 OpenCount = 0, BPCDOCount = 0, UnloadedCount = 0;
	for (const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& E : Entries)
	{
		switch (E.Source)
		{
			case EStaleQuestTagSource::LoadedLevelInstance:   ++OpenCount;     break;
			case EStaleQuestTagSource::ActorBlueprintCDO:     ++BPCDOCount;    break;
			case EStaleQuestTagSource::UnloadedLevelInstance: ++UnloadedCount; break;
			default: break;
		}
	}

	// Per-entry log (Warning so it surfaces in default verbosity).
	UE_LOG(LogSimpleQuest, Display, TEXT("StaleQuestTagsScan: ---- %d stale reference(s) ----"), Entries.Num());
	for (const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& E : Entries)
	{
		UE_LOG(LogSimpleQuest, Warning,
			TEXT("StaleQuestTagsScan: [%s] actor=%s component=%s field=%s tag=%s package=%s"),
			SourceText(E.Source),
			E.Actor.IsValid()     ? *E.Actor->GetName()                  : TEXT("(null)"),
			E.Component.IsValid() ? *E.Component->GetClass()->GetName()  : TEXT("(null)"),
			*E.FieldLabel,
			*E.StaleTag.ToString(),
			*E.PackagePath);
	}

	UE_LOG(LogSimpleQuest, Display,
		TEXT("StaleQuestTagsScan: summary — Open=%d, BPCDOs=%d, Unloaded=%d, Total=%d"),
		OpenCount, BPCDOCount, UnloadedCount, Entries.Num());

	// Optional JSON output.
	if (!OutputJsonPath.IsEmpty())
	{
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("totalCount"),    Entries.Num());
		Writer->WriteValue(TEXT("openCount"),     OpenCount);
		Writer->WriteValue(TEXT("bpCDOCount"),    BPCDOCount);
		Writer->WriteValue(TEXT("unloadedCount"), UnloadedCount);
		Writer->WriteArrayStart(TEXT("entries"));
		for (const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& E : Entries)
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("source"),    SourceText(E.Source));
			Writer->WriteValue(TEXT("actor"),     E.Actor.IsValid()     ? E.Actor->GetName()                 : FString());
			Writer->WriteValue(TEXT("component"), E.Component.IsValid() ? E.Component->GetClass()->GetName() : FString());
			Writer->WriteValue(TEXT("field"),     E.FieldLabel);
			Writer->WriteValue(TEXT("tag"),       E.StaleTag.ToString());
			Writer->WriteValue(TEXT("package"),   E.PackagePath);
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();

		if (FFileHelper::SaveStringToFile(JsonString, *OutputJsonPath))
		{
			UE_LOG(LogSimpleQuest, Display, TEXT("StaleQuestTagsScan: wrote JSON results to %s"), *OutputJsonPath);
		}
		else
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("StaleQuestTagsScan: failed to write JSON results to %s"), *OutputJsonPath);
			return -1;
		}
	}

	// Exit code: 0 = clean, 1 = stale found.
	return Entries.Num() > 0 ? 1 : 0;
}