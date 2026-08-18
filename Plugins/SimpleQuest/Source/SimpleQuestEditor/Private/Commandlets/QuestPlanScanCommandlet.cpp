// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Commandlets/QuestPlanScanCommandlet.h"

#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestExportOutput.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestImportOperations.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestPlanReport.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
	/** Every marker folder under Root, minus the engine scratch trees. Sorted, so discovery order is never inherited. */
	TArray<FString> FindMarkerFolders(const FString& Root)
	{
		TArray<FString> MarkerFiles;
		IFileManager::Get().FindFilesRecursive(MarkerFiles, *Root, GQuestExportMarkerName, /*Files*/ true, /*Dirs*/ false);

		TArray<FString> Folders;
		for (const FString& File : MarkerFiles)
		{
			// Normalized before matching, because FindFilesRecursive returns platform separators and a Windows path would
			// otherwise never match these.
			FString Normalized = File;
			FPaths::NormalizeDirectoryName(Normalized);
			if (Normalized.Contains(TEXT("/Saved/")) || Normalized.Contains(TEXT("/Intermediate/")))
			{
				UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("QuestPlanScan: skipping engine scratch '%s'."), *File);
				continue;
			}
			Folders.Add(FPaths::GetPath(Normalized));
		}
		Folders.Sort();
		return Folders;
	}
}

UQuestPlanScanCommandlet::UQuestPlanScanCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;     // GEditor must exist: we sync-load questline assets and run the editor-side planner. Without
	// this, UE 5.6's TedsCore plugin asserts on GEditor during the startup-modules-loaded
	// callback before Main() even runs.
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UQuestPlanScanCommandlet::Main(const FString& Params)
{
	FString Root;
	FString OutputJsonPath;
	FString FailOn = TEXT("refusals");
	FParse::Value(*Params, TEXT("Root="), Root);
	FParse::Value(*Params, TEXT("OutputJson="), OutputJsonPath);
	FParse::Value(*Params, TEXT("FailOn="), FailOn);
	FailOn = FailOn.ToLower();

	if (Root.IsEmpty())
	{
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("QuestPlanScan: -Root=<dir> is required. Nothing scanned."));
		return 2;
	}
	if (FailOn != TEXT("refusals") && FailOn != TEXT("differences"))
	{
		// Refused rather than defaulted: a misspelled mode that silently became the lenient one is a gate that quietly
		// stopped gating, which is the failure this whole tool exists to prevent.
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("QuestPlanScan: -FailOn='%s' is not a mode. Use 'refusals' or "
			"'differences'. Nothing scanned."), *FailOn);
		return 2;
	}

	if (FPaths::IsRelative(Root))           { Root = FPaths::Combine(FPaths::ProjectDir(), Root); }
	if (FPaths::IsRelative(OutputJsonPath) && !OutputJsonPath.IsEmpty())
	{
		OutputJsonPath = FPaths::Combine(FPaths::ProjectDir(), OutputJsonPath);
	}
	FPaths::NormalizeDirectoryName(Root);

	if (!IFileManager::Get().DirectoryExists(*Root))
	{
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("QuestPlanScan: root '%s' does not exist. Nothing scanned."), *Root);
		return 2;
	}

	const TArray<FString> Folders = FindMarkerFolders(Root);
	if (Folders.Num() == 0)
	{
		// A GATE THAT CHECKED NOTHING MUST NOT REPORT SUCCESS. Zero markers is far more often a mistyped root than a
		// genuinely empty corpus, and returning 0 here is how that mistake survives a month of green builds.
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("QuestPlanScan: no export markers found under '%s'. A run with "
			"nothing to check reports no verdict. Point -Root at a folder holding exported quest data."), *Root);
		return 2;
	}

	UE_LOG(LogSimpleQuestResolver, Display, TEXT("QuestPlanScan: %d corpus folder(s) under '%s', failing on %s."),
		Folders.Num(), *Root, *FailOn);

	TArray<FQuestPlanRunItem> Items;
	for (const FString& Folder : Folders)
	{
		FQuestExportMarker Marker;
		if (!ReadQuestExportMarker(Folder, Marker))
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("QuestPlanScan: '%s' had a marker a moment ago and does not "
				"now. Skipped."), *Folder);
			continue;
		}
		// Owned is deliberately NOT consulted. It says whether an export may REPLACE this folder; reading one is
		// something anybody may do, and a studio's own corpus declaring itself unowned is exactly who this is for.

		FQuestPlanRunItem Item;
		Item.Questline = Marker.SourceAsset;

		FString RelativeFolder = Folder;
		FPaths::MakePathRelativeTo(RelativeFolder, *(Root / TEXT("")));
		Item.Source.Folder  = RelativeFolder;
		Item.Source.Format  = Marker.Format;
		Item.Source.Mapping = Marker.Mapping;

		FQuestDataEndpoint Endpoint;
		Endpoint.Kind       = EQuestEndpointKind::ForeignFile;
		Endpoint.FormatName = Marker.Format;
		Endpoint.Folder     = Folder;

		const UQuestImportMapping* Mapping = Marker.Mapping.IsEmpty()
			? nullptr
			: Cast<UQuestImportMapping>(FSoftObjectPath(Marker.Mapping).TryLoad());
		if (!Marker.Mapping.IsEmpty() && !Mapping)
		{
			Item.Plan.Refusals.Add(FString::Printf(TEXT("the Mapping '%s' this folder names did not load - the corpus "
				"cannot be read in the vocabulary it was written in"), *Marker.Mapping));
			Items.Add(MoveTemp(Item));
			continue;
		}

		// READ AND VALIDATE FIRST, ALWAYS - including for corpora whose asset exists and will be planned below, which
		// re-reads. The duplication is deliberate and is about UNIFORMITY, not speed: "is this source sound" is asked
		// the same way for every corpus, so an uncreated one and a planned one cannot answer it from different code.
		FQuestDataBundle Bundle;
		TMap<FString, const FQuestDataRow*> NodeRowsByKey;
		TSet<FString> AllRowKeys;
		TArray<FString> ReadWarnings;
		FString ReadError;
		if (!QuestImport_ReadAndValidate(Endpoint, Mapping, Bundle, NodeRowsByKey, AllRowKeys, ReadWarnings, ReadError))
		{
			// Into Plan.Refusals rather than a channel of its own: a refusal is a refusal whether it was earned by
			// reading the source or by comparing it, and one channel is what lets a consumer ask "what is wrong here"
			// without first asking how far the run got.
			Item.Plan.Refusals.Add(ReadError);
			Item.Plan.Warnings.Append(ReadWarnings);
			Items.Add(MoveTemp(Item));
			continue;
		}
		Item.Source.RowCount = AllRowKeys.Num();
		Item.Plan.Warnings.Append(ReadWarnings);

		UQuestlineGraph* Target = Cast<UQuestlineGraph>(FSoftObjectPath(Marker.SourceAsset).TryLoad());
		if (!Target || !Target->QuestlineEdGraph)
		{
			// NOT AN ERROR. Progression data authored before anyone builds the questline is a normal, common state, and
			// the corpus has just been proven sound on its own terms. bPlanned stays false and the report says
			// "validated" - which is a different answer from "matches", and must not print like one.
			UE_LOG(LogSimpleQuestResolver, Display, TEXT("QuestPlanScan: '%s' describes '%s', which does not exist yet - "
				"source validated, nothing to compare against."), *Item.Source.Folder, *Marker.SourceAsset);
			Items.Add(MoveTemp(Item));
			continue;
		}

		FQuestImportRequest Request;
		Request.Endpoint = Endpoint;
		Request.Mapping  = Mapping;
		Request.Policies = QuestImport_ResolvePolicies(Mapping, /*bResetAbsent*/ false);

		FQuestImportOutcome Outcome;
		if (!QuestImport_RunInPlace(*Target, Request, /*bApply*/ false, Outcome))
		{
			Item.Plan.Refusals.Add(Outcome.Error.IsEmpty() ? TEXT("planning failed without stating a reason") : Outcome.Error);
			Items.Add(MoveTemp(Item));
			continue;
		}

		const int32 RowCount = Item.Source.RowCount;   // survives the plan assignment below
		Item.Plan     = MoveTemp(Outcome.Plan);
		Item.bPlanned = true;
		Item.Source.RowCount = RowCount;
		Item.Plan.Warnings.Append(Outcome.Warnings);
		Items.Add(MoveTemp(Item));
	}

	// PER-CORPUS LOG. CI shows a log tail long before anyone opens an artifact, so the console is a second product
	// rather than a lesser one - and it goes through the SAME renderer the panel and the console command use.
	int32 Refused = 0, Differing = 0, Validated = 0, Clean = 0;
	for (const FQuestPlanRunItem& Item : Items)
	{
		LogQuestPlanReport(Item.Plan, EQuestPlanSubject::Questline,
			FString::Printf(TEXT("QuestPlanScan [%s]"), *Item.Source.Folder));

		if (Item.Plan.Refusals.Num() > 0) { ++Refused; }
		else if (!Item.bPlanned)          { ++Validated; }
		else if (Item.Plan.IsNoOp())      { ++Clean; }
		else                              { ++Differing; }
	}

	UE_LOG(LogSimpleQuestResolver, Display, TEXT("QuestPlanScan: %d corpus folder(s) — %d clean, %d with differences, "
		"%d validated (no asset yet), %d refused."), Items.Num(), Clean, Differing, Validated, Refused);

	if (!OutputJsonPath.IsEmpty())
	{
		// REPORTED relative to the project. Root is absolute by now because the filesystem needs it that way, but an
		// absolute path is a MACHINE path, and the artifact's whole value is that two checkouts scanning the same corpus
		// produce the same bytes. Falls back to the absolute form only for a corpus genuinely outside the project - a
		// sibling repo on another drive - where no portable spelling exists to offer.
		FString ReportedRoot = Root;
		if (!FPaths::MakePathRelativeTo(ReportedRoot, *FPaths::ProjectDir()))
		{
			ReportedRoot = Root;
		}

		const FString Json = BuildQuestPlanRunJson(Items, ReportedRoot, FailOn);
		if (!FFileHelper::SaveStringToFile(Json, *OutputJsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("QuestPlanScan: failed to write '%s'."), *OutputJsonPath);
			return 2;
		}
		UE_LOG(LogSimpleQuestResolver, Display, TEXT("QuestPlanScan: wrote '%s'."), *OutputJsonPath);
	}

	// A refusal fails under EITHER mode - it means the corpus cannot be applied at all, which no policy calls fine.
	// Differences fail only when the caller said their files are the source of truth.
	if (Refused > 0) { return 1; }
	if (FailOn == TEXT("differences") && Differing > 0) { return 1; }
	return 0;
}

