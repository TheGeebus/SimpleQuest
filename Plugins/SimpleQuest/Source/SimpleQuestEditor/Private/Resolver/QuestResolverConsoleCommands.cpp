// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// The console surface of the resolver: argument parsing, human-readable logging, and the registrations for
// ImportQuestline, ExportQuestline and EnumerateSourceColumns. It holds no model code - every operation it drives lives
// in Resolver/, so the console is one caller among several rather than the implementation. That separation is what lets
// the toolbar and a headless commandlet run the same pipeline without going through a command string.
//
// The logging lives HERE rather than with the planner deliberately. A plan comes back as data, and the caller decides
// how to say it: this file prints it, the panel renders it, and a commandlet could serialize it - none of them is the
// privileged one.

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/DataTable.h"
#include "Factories/QuestlineGraphFactory.h"
#include "IAssetTools.h"
#include "ISimpleQuestEditorModule.h"
#include "QuestExportOperations.h"
#include "QuestPlanReport.h"
#include "Internationalization/Text.h"
#include "Misc/Paths.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestGraphBuilder.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestImportOperations.h"
#include "Resolver/QuestInPlacePlan.h"
#include "Resolver/QuestInstancedChildren.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestPlanBroker.h"
#include "Resolver/QuestRowApply.h"
#include "Resolver/QuestRowRestore.h"
#include "SimpleQuestLog.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Utilities/QuestlineGraphCompiler.h"


/** A console-typed asset path is usually the short form "/Game/Path/Asset"; FSoftObjectPath needs "/Game/Path/Asset.Asset". */
static FString NormalizeConsoleAssetPath(const FString& In)
{
	if (In.Contains(TEXT("."))) return In;
	FString Ignored, AssetName;
	if (In.Split(TEXT("/"), &Ignored, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		return In + TEXT(".") + AssetName;
	}
	return In;
}

static const TCHAR* PlanActionName(EQuestNodePlanAction Action)
{
	switch (Action)
	{
	case EQuestNodePlanAction::Create: return TEXT("CREATE");
	case EQuestNodePlanAction::Orphan: return TEXT("ORPHAN");
	default:                           return TEXT("UPDATE");
	}
}

static void ImportQuestlineCmd(const TArray<FString>& Args)
{
	// Separate the optional "--format=<name>" arg from the positional path args BEFORE rejoining (it must not get
	// swept into the space-rejoined folder path). PathArgs = every arg that isn't a --flag.
	// --datatable=<AssetPath> selects the asset provenance; then no source FOLDER is needed (the dest path is the only
	// positional arg). Otherwise the folder is the positional args before the dest, space-rejoined.
	FString DataTablePath;
	FString InPlacePath;
	for (const FString& Arg : Args)
	{
		if (Arg.StartsWith(TEXT("--datatable="))) DataTablePath = Arg.RightChop(12);
		else if (Arg.StartsWith(TEXT("--in-place="))) InPlacePath = Arg.RightChop(11);
	}
	const bool bInPlace = !InPlacePath.IsEmpty();
	const bool bApply = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--apply"), ESearchCase::IgnoreCase); });
	const bool bResetAbsent = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--reset-absent"), ESearchCase::IgnoreCase); });
	const bool bDeleteOrphans = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--delete-orphans"), ESearchCase::IgnoreCase); });

	TArray<FString> PathArgs;
	for (const FString& Arg : Args)
	{
		if (!Arg.StartsWith(TEXT("--"))) PathArgs.Add(Arg);
	}
	// A source folder unless the DataTable provenance supplies it, plus a dest package unless --in-place names the target.
	const int32 MinPositional = (DataTablePath.IsEmpty() ? 1 : 0) + (bInPlace ? 0 : 1);
	if (PathArgs.Num() < MinPositional)
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: usage 'SimpleQuest.ImportQuestline <FolderPath> <DestPackagePath> [--format=json] [--mapping=<asset>]' "
			"or 'SimpleQuest.ImportQuestline <DestPackagePath> --datatable=<asset> [--mapping=<asset>]'. "
			"Add '--in-place=<AssetPath>' to compare against an existing asset instead of creating one; the dest package arg is then omitted."));
		return;
	}

	// --in-place takes no dest package arg, but the create form does - so adapting one command into the other easily
	// leaves the dest behind. Space-rejoining would swallow it into the folder path, and the only symptom would be a
	// "folder not found" naming a path the caller never typed. Name the actual mistake instead. Guarded on there being
	// more than one positional arg so a genuine root-anchored source folder is never mistaken for a package path.
	if (bInPlace && PathArgs.Num() > 1 && FPackageName::IsValidLongPackageName(PathArgs.Last()))
	{
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: trailing argument '%s' is a package path, which --in-place does not take - the "
			"target is named by --in-place=<AssetPath>. Pass only the source folder. Nothing was modified."), *PathArgs.Last());
		return;
	}

	// Console arg tokenization splits on whitespace and does NOT honor quotes, so a folder path containing spaces
	// (e.g. "E:/Unreal Projects/...") arrives as multiple Args. The dest package path is the LAST path arg (never has
	// spaces - it's a /Game/... mount path); the folder path is everything before it, rejoined with spaces. With
	// --in-place there is no dest arg to peel off, so the whole positional remainder is the folder.
	const FString DestPackagePath = bInPlace ? FString() : PathArgs.Last();
	FString FolderPath;
	if (DataTablePath.IsEmpty())
	{
		TArray<FString> FolderParts = PathArgs;
		if (!bInPlace) FolderParts.Pop();                 // drop the dest path
		FolderPath = FString::Join(FolderParts, TEXT(" "));
		FolderPath = FolderPath.TrimQuotes();             // tolerate quotes if the caller added them
	}

	// The source is an ENDPOINT: a file folder read through a format provider, or a DataTable asset. One read call, so
	// the import path never branches on provenance again.
	FQuestDataEndpoint Endpoint;
	if (!DataTablePath.IsEmpty())
	{
		// A console-typed asset path is usually the short form; normalize so --datatable accepts the same form --mapping does.
		Endpoint.Kind = EQuestEndpointKind::DataTable;
		Endpoint.Table = TSoftObjectPtr<UDataTable>(FSoftObjectPath(NormalizeConsoleAssetPath(DataTablePath)));
	}
	else
	{
		const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args, TEXT("ImportQuestline"));
		if (!Format)
		{
			return;   // the unregistered-format error was already logged; refuse before creating anything.
		}
		Endpoint.Kind = EQuestEndpointKind::ForeignFile;
		Endpoint.FormatName = Format->FormatName();
		Endpoint.Folder = FolderPath;
	}

	// Read, translate and validate in one call, shared with the in-place branch below so the pipeline has a single
	// definition. The three failure modes still read differently because the operation phrases its own error.
	FQuestDataBundle Bundle;
	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	TSet<FString> AllRowKeys;
	TArray<FString> Warnings;
	FString ReadError;
	// Loaded once and shared: the three publish sites below all have to report the SAME mapping, and calling the loader
	// per site is how two of them would eventually disagree.
	const UQuestImportMapping* ArgMapping = LoadQuestMappingArg(Args);
	if (!QuestImport_ReadAndValidate(Endpoint, ArgMapping, Bundle, NodeRowsByKey, AllRowKeys, Warnings, ReadError))
	{
		// "No asset created" is fresh-create wording and is a lie on an --in-place run, which was never going to create
		// one. Same failure, two modes, two accurate endings.
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: %s. %s"), *ReadError,
			bInPlace ? TEXT("Nothing was modified.") : TEXT("No asset created."));

		// Published from HERE as well as from the in-place branch below, because THIS is where the common failure
		// lands: an unreadable source never reaches the branch that knows it is in-place. A publish only at the later
		// site would surface mapping and validation failures and silently miss the one a designer hits most.
		if (bInPlace)
		{
			FQuestPlanBroker::Get().PublishFailure(NormalizeConsoleAssetPath(InPlacePath), ReadError, QuestPlanSourceFromEndpoint(Endpoint, ArgMapping));
		}
		return;
	}

	// In-place: describe what a re-import WOULD do to the existing asset, then stop. Planning is read-only and is the
	// only thing --in-place does; applying a plan is a separate, explicitly-requested step. That ordering means a
	// mistyped target path can never damage an asset. The plan is produced as data rather than only logged, so the
	// editor action can render exactly what the console prints.
	if (bInPlace)
	{
		const FString AssetPath = NormalizeConsoleAssetPath(InPlacePath);
		UQuestlineGraph* TargetGraph = Cast<UQuestlineGraph>(FSoftObjectPath(AssetPath).TryLoad());
		if (!TargetGraph || !TargetGraph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: --in-place target '%s' did not load as a questline graph. Nothing was modified."), *AssetPath);
			return;
		}

		FQuestInPlacePlan Plan;
		FQuestImportRequest Request;
		Request.Endpoint = Endpoint;
		Request.Mapping = ArgMapping;
		Request.bDeleteOrphans = bDeleteOrphans;
		// Resolved out here rather than inside the run, because the mode has to be REPORTED before any work happens:
		// a plan is only interpretable against the policy that produced it, and the same source and asset yield
		// different plans under Preserve and Reset.
		Request.Policies = QuestImport_ResolvePolicies(Request.Mapping, bResetAbsent);

		const TCHAR* PolicyName =
			Request.Policies.Default == EQuestAbsentFieldPolicy::Reset   ? TEXT("Reset") :
			Request.Policies.Default == EQuestAbsentFieldPolicy::Require ? TEXT("Require") : TEXT("Preserve");
		const bool bWouldDeleteOrphans =
			bDeleteOrphans || (Request.Mapping && Request.Mapping->bDeleteOrphanedNodes);
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: in-place %s - source '%s', absent-field policy %s%s.%s"),
			bApply ? TEXT("APPLY") : TEXT("PLAN (read-only)"),
			DataTablePath.IsEmpty() ? *FolderPath : *DataTablePath,
			PolicyName,
			bResetAbsent ? TEXT(" (via --reset-absent)") : TEXT(""),
			bApply && bWouldDeleteOrphans ? TEXT(" [WILL DELETE ORPHANS]") : TEXT(""));

		// The transaction wraps the call because the run applies internally. A plan carrying refusals applies nothing,
		// so on that path this opens and closes with no object recorded - which the transaction buffer discards.
		TUniquePtr<FScopedTransaction> Transaction;
		if (bApply)
		{
			Transaction = MakeUnique<FScopedTransaction>(
				NSLOCTEXT("SimpleQuestEditor", "ApplyInPlaceImport", "Apply In-Place Import"));
		}

		FQuestImportOutcome Outcome;
		if (!QuestImport_RunInPlace(*TargetGraph, Request, bApply, Outcome))
		{
			// Published as well as logged. Picking the wrong format is now one click, and a failure that only reaches the
			// log leaves the panel saying "no plan has been computed" - which reads as "try again" for the thing that just
			// failed. The notification catches the eye; the panel is where the reason stays.
			FQuestPlanBroker::Get().PublishFailure(TargetGraph->GetPathName(), Outcome.Error, QuestPlanSourceFromEndpoint(Request.Endpoint, Request.Mapping));
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: %s. Nothing was modified."), *Outcome.Error);
			return;
		}

		Outcome.Plan.TargetAssetPath = AssetPath;
		LogQuestPlanReport(Outcome.Plan, EQuestPlanSubject::Questline, TEXT("ImportQuestline"));
		// The log is one rendering of the plan; the panel is another. Published unconditionally, including for a plan
		// about to be applied, so the panel always shows what the run actually decided.
		FQuestPlanBroker::Get().Publish(Outcome.Plan.TargetAssetPath, Outcome.Plan,	QuestPlanSourceFromEndpoint(Endpoint, ArgMapping));

		if (!bApply)
		{
			return;   // planning is the default; mutating is opted into
		}

		// A plan carrying refusals or contested keys is not trustworthy in ANY part - those say the planner could not
		// describe the source, not merely that one row is odd. ApplyQuestPlan already declined; this reports why.
		if (Outcome.ApplyResult.bRefused)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: --apply refused - the plan carries %d refusal(s) and %d contested key(s). "
				"Resolve those and re-plan. Nothing was modified."),
				Outcome.Plan.Refusals.Num(),
				Outcome.Plan.AmbiguousKeys.Num());
			return;
		}

		const FQuestApplyResult& Result = Outcome.ApplyResult;

		for (const FString& S : Result.Skipped) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: apply skipped %s"), *S);
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: APPLIED to '%s' - %d property change(s), %d node(s) created, "
			"%d node(s) moved, %d wire edge(s) changed, %d node(s) DELETED. %d entry/entries deferred by policy, %d skipped."),
			*AssetPath,
			Result.PropertiesWritten,
			Result.EntitiesCreated,
			Result.NodesMoved,
			Result.EdgesChanged,
			Result.EntitiesDeleted,
			Result.EntriesDeferred,
			Result.Skipped.Num());

		if (Result.GraphsCompiled > 0)
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: recompiled %d graph(s) (target + linked neighborhood)."),
				Result.GraphsCompiled);
		}
		if (!Result.bCompileSucceeded)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: the apply landed but a recompile FAILED - the asset is "
				"modified and needs a manual compile. See the Quest Compiler log."));
		}

		// Only dirty the package if something actually happened. A re-import that changes nothing should leave no trace:
		// marking it regardless makes every no-op run look like a modification, which costs a save and a diff for work
		// that was not done - and trains a designer to ignore the one signal that says an asset moved.
		const bool bChangedAnything = Result.ChangedAnything();
		if (bChangedAnything)
		{
			TargetGraph->GetPackage()->MarkPackageDirty();
			
			// The plan has now happened, so it stops being a plan. Same rule the toolkit follows, and it matters more here:
			// a console apply with a panel open would otherwise leave that panel showing work it just performed.
			FQuestPlanBroker::Get().Clear(Outcome.Plan.TargetAssetPath);
		}
		else if (Result.EntriesDeferred > 0 || Result.Skipped.Num() > 0)
		{
			// Nothing was WRITTEN, but the asset does not match the source either. "Already matches" is the one line a
			// designer acts on by stopping, so it must never appear while work remains. The package still stays clean,
			// because nothing changed - that half was right.
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: nothing was applied, and the asset does NOT match the "
				"source - %d entry/entries could not be performed. Package left clean."),
				Result.EntriesDeferred + Result.Skipped.Num());
		}
		else
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: nothing to apply - the asset already matches the source. "
				"Package left clean."));
		}
		return;
	}

	// P1 - create the asset via the factory, then restore the self row (with _RT identity + instanced rewards).
	// Two distinct identities: the ROW KEY (sanitized EffectiveID - folder name, tag namespace) and the authored
	// QuestlineID FIELD (raw, whatever the designer typed, spaces and all - the compiler sanitizes it only when
	// building tags, never mutating the field). The asset NAME rides the sanitized key (a package name can't hold
	// spaces); the QuestlineID FIELD must preserve the raw authored value so the round-trip doesn't alter it.
	const FQuestDataRow& SelfRow = Bundle.TablesByType[TEXT("questline_graph")].Rows[0];
	const FString OriginalKey = SelfRow.Key;                          // sanitized - folder/tag identity
	const FString RawQuestlineID = SelfRow.Get(TEXT("QuestlineID"));  // raw authored field (may be empty)
	const FString AssetName = OriginalKey + TEXT("_RT");              // _RT so the compiled tag namespace doesn't collide.

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UQuestlineGraphFactory* Factory = NewObject<UQuestlineGraphFactory>();
	UObject* Created = AssetTools.CreateAsset(AssetName, DestPackagePath, UQuestlineGraph::StaticClass(), Factory);
	UQuestlineGraph* Graph = Cast<UQuestlineGraph>(Created);
	if (!Graph || !Graph->QuestlineEdGraph)
	{
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: asset creation failed at '%s/%s'."), *DestPackagePath, *AssetName);
		return;
	}

	TSet<FString> Consumed;

	// Self-row properties onto the graph object (QuestlineID gets _RT; instanced QuestlineRewards rebuilt).
	RestoreQuestRowProperties(Graph, SelfRow);
	{
		// QuestlineID handling for the round-trip. Two cases, because GetEffectiveID() falls back to the ASSET
		// NAME when the field is empty:
		//   - Source field NON-empty: set the RT field to <raw>_RT, so re-export's QuestlineID cell matches the
		//     source's modulo _RT.
		//   - Source field EMPTY (asset-name-derived): LEAVE IT EMPTY. The source's EffectiveID was its asset
		//     name (e.g. "QL_Ch5_Blocking"); the RT asset's name is "<name>_RT", so the same empty->asset-name
		//     fallback yields "<name>_RT" - matching the source modulo _RT. Writing the literal "_RT" here (the
		//     prior bug) would make QuestlineID = "_RT", tags = SimpleQuest.Questline._RT.*, and the export folder
		//     "_RT" - diverging from the asset-name identity the source actually used.
		if (!RawQuestlineID.IsEmpty())
		{
			if (FProperty* IDProp = Graph->GetClass()->FindPropertyByName(TEXT("QuestlineID")))
			{
				const FString RT = RawQuestlineID + TEXT("_RT");
				IDProp->ImportText_Direct(*RT, IDProp->ContainerPtrToValuePtr<void>(Graph), nullptr, PPF_None);
			}
		}
		// else: RestoreQuestRowProperties already left it empty (the source cell was empty) - nothing to do.
	}
	ReattachQuestInstancedChildren(Graph, OriginalKey, Bundle, Consumed, Warnings);   // self-row child keys are prefixed by the self key

	// P2 - spawn nodes, root graph first, recursing into container inner graphs.
	TMap<FString, UEdGraphNode*> NodeByKey;
	ImportQuestGraphLevel(Graph->QuestlineEdGraph, TEXT("root"), Bundle, NodeRowsByKey, NodeByKey, Consumed, Warnings);

	// P3 - pin refresh pass (innermost-first).
	RefreshQuestNodePins(Bundle, NodeRowsByKey, NodeByKey, Warnings);

	// P4 - wire edges + contains-edge cross-check.
	WireQuestEdges(Bundle, NodeByKey, Consumed, Warnings);

	// Wrap the double-compile in a compile batch so the gameplay-tag-tree rebuild coalesces to ONCE (at EndCompileBatch)
	// instead of once PER compile pass. The batch's incremental AddNativeTagsForGraph keeps pass 2's RequestGameplayTag
	// lookups valid against pass 1's registrations (that's exactly what the first-compile-identity double-compile needs),
	// while the expensive tree rebuild + INI write defer to batch end. Same mechanism CompileAllQuestlineGraphs uses.
	ISimpleQuestEditorModule::Get().BeginCompileBatch();
	TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
	Compiler->Compile(Graph);                          // pass 1: registers the identity + state tags
	const bool bCompiled = Compiler->Compile(Graph);   // pass 2: identity now valid -> complete resolution records
	ISimpleQuestEditorModule::Get().EndCompileBatch();

	UPackage* Package = Graph->GetPackage();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Graph);
	const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Graph, *FileName, SaveArgs);

	for (const FString& W : Warnings) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: %s"), *W);
	UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: '%s' -> '%s/%s' - %d node(s), %d edge(s), %d warning(s), compile %s. Run C (re-export + diff) and B2 (DumpCompiled + diff) to verify."),
		*OriginalKey,
		*DestPackagePath,
		*AssetName,
		NodeByKey.Num(),
		Bundle.Edges.Num(),
		Warnings.Num(),
		bCompiled ? TEXT("OK") : TEXT("FAILED"));
}

// The destination folder, or empty for the derived one. POSITIONAL rather than a --out= flag because the console
// tokenizes on whitespace and does not honor quotes, and this project's own paths contain spaces - the same reason
// ImportQuestline takes its source folder positionally and rejoins it. Everything after the asset path that is not a
// flag belongs to the folder.
static FString ResolveQuestExportDestinationFromArgs(const TArray<FString>& Args)
{
	TArray<FString> Parts;
	for (int32 Index = 1; Index < Args.Num(); ++Index)
	{
		if (!Args[Index].StartsWith(TEXT("--"))) { Parts.Add(Args[Index]); }
	}
	return FString::Join(Parts, TEXT(" ")).TrimStartAndEnd().TrimQuotes();
}

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

	// --datatable=<AssetPath> points this at a studio's table instead of a folder, and destination ownership takes it
	// from there: a folder is replaced wholesale, a table is PLANNED and then applied.
	FString DataTablePath;
	for (const FString& Arg : Args)
	{
		if (Arg.StartsWith(TEXT("--datatable="))) { DataTablePath = Arg.RightChop(12); }
	}
	const bool bApply = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--apply"), ESearchCase::IgnoreCase); });

	FQuestExportRequest Request;
	Request.Graph = Graph;
	Request.Mapping = LoadQuestMappingArg(Args);

	if (!DataTablePath.IsEmpty())
	{
		// A table carries its own layout, so no format is resolved at all - resolving one would invite a --format that
		// silently does nothing. Normalized so --datatable accepts the same spelling --mapping does.
		Request.Endpoint.Kind = EQuestEndpointKind::DataTable;
		Request.Endpoint.Table = TSoftObjectPtr<UDataTable>(FSoftObjectPath(NormalizeConsoleAssetPath(DataTablePath)));
	}
	else
	{
		// Resolved to a NAME here rather than a provider, because the operation takes the name - the console reads it
		// from --format, a toolbar button reads it from a combo, and neither needs to know the registry exists.
		const FString FormatName = ResolveQuestFormatNameFromArgs(Args, TEXT("ExportQuestline"));
		if (FormatName.IsEmpty())
		{
			return;   // the unregistered-format error was already logged; nothing exported.
		}
		Request.Endpoint.Kind = EQuestEndpointKind::ForeignFile;
		Request.Endpoint.FormatName = FormatName;
		Request.Endpoint.Folder = ResolveQuestExportDestinationFromArgs(Args);   // empty => derived
	}

	FQuestExportOutcome Out;
	const bool bOk = QuestExport_Run(Request, Out);

	// Warnings first either way: a reverse-mapping warning explains the shape of what was written, and on a refusal it
	// may well explain the refusal.
	for (const FString& W : Out.Warnings)
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ExportQuestline: %s"), *W);
	}

	if (!bOk)
	{
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: %s"), *Out.Error);
		FQuestPlanBroker::Get().PublishExport(Graph->GetPathName(), FString(), Out.Error);
		return;
	}

	// THE TABLE ARM. Nothing has been written - what came back is a PLAN, reported exactly the way the import direction
	// reports one so the two read alike, and published so the panel can show it.
	if (Out.bPlanned)
	{
		LogQuestPlanReport(Out.RowPlan, EQuestPlanSubject::Row, TEXT("ExportQuestline"));
		FQuestPlanBroker::Get().Publish(Graph->GetPathName(), Out.RowPlan, QuestPlanSourceFromEndpoint(Request.Endpoint, Request.Mapping));

		if (!bApply)
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ExportQuestline: nothing was written. Re-run with --apply."));
			return;
		}

		UDataTable* Destination = Request.Endpoint.Table.LoadSynchronous();
		if (!Destination)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: --apply could not load '%s'. Nothing written."), *DataTablePath);
			return;
		}

		TMap<FString, const FQuestDataRow*> RowsByKey;
		if (const FQuestDataTable* Content = Out.PlannedBundle.TablesByType.Find(TEXT("content")))
		{
			for (const FQuestDataRow& R : Content->Rows) { RowsByKey.Add(R.Key, &R); }
		}

		// The CALLER owns the transaction - the applier opens none, so an apply and anything around it undo as one unit.
		FScopedTransaction Transaction(NSLOCTEXT("SimpleQuest", "ExportRowsApply", "Write questline rows into a data table"));
		FQuestApplyResult Result;
		ApplyQuestRowPlan(*Destination, Out.RowPlan, RowsByKey, Result);

		for (const FString& S : Result.Skipped)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ExportQuestline: apply skipped %s"), *S);
		}
		if (Result.bRefused)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ExportQuestline: --apply refused - the plan carries %d refusal(s) and "
				"%d contested key(s). Nothing was written."), Out.RowPlan.Refusals.Num(), Out.RowPlan.AmbiguousKeys.Num());
			return;
		}

		// Dirty only when something actually changed, so a no-op apply leaves the asset genuinely untouched.
		if (Result.ChangedAnything()) { Destination->MarkPackageDirty(); }

		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ExportQuestline: WROTE into '%s' — %d row(s) created, %d field(s) written, "
			"%d skipped."), *DataTablePath, Result.EntitiesCreated, Result.PropertiesWritten, Result.Skipped.Num());

		FQuestPlanBroker::Get().Clear(Graph->GetPathName());
		return;
	}

	UE_LOG(LogSimpleQuestResolver, Log, TEXT("ExportQuestline: '%s' — %d entity row(s) across %d type(s), %d edge(s), %d knot(s) "
		"collapsed. Wrote %d file(s) to '%s'%s; removed %d from the previous export."),
		*Out.ExportKey,
		Out.EntityRows,
		Out.TypeCount,
		Out.EdgeCount,
		Out.KnotsCollapsed,
		Out.FilesWritten,
		*Out.OutDir,
		Out.bDestinationDerived ? TEXT(" (default destination)") : TEXT(""),
		Out.FilesRemoved);

	FQuestPlanBroker::Get().PublishExport(Graph->GetPathName(), FString::Printf(TEXT("Exported %d file(s) to '%s'%s"),
		Out.FilesWritten,
		*Out.OutDir,
		Out.bDestinationDerived ? TEXT(" (default destination)") : TEXT("")),
	FString());
}

static FAutoConsoleCommand GImportQuestlineCmd(
	TEXT("SimpleQuest.ImportQuestline"),
	TEXT("Reconstruct a questline asset from an interlingua table folder (an ExportQuestline output) and "
		"compile it. Creates a fresh <QuestlineID>_RT asset. Args: <FolderPath> <DestPackagePath> (e.g. "
		"\"E:/.../Saved/QuestExport/QL_Ch1_BasicTrigger\" /Game/Imported)."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ImportQuestlineCmd));

static FAutoConsoleCommand GEnumerateSourceColumnsCmd(
	TEXT("SimpleQuest.EnumerateSourceColumns"),
	TEXT("List the columns a foreign source exposes (proves the source-column provider seam). "
		"Args: <SourceFolder> [--format=<name>] (default TSV)."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("EnumerateSourceColumns: usage <SourceFolder> [--format=<name>]"));
			return;
		}
		// The console tokenizes on whitespace and strips quotes, so a path with spaces arrives as MULTIPLE args. Re-join all
		// non-flag args with spaces to reconstruct the folder (quoted or not); --format=<name> is the only recognized flag.
		FString Folder;
		FString FormatName = TEXT("TSV");
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--format=")))
			{
				FormatName = Arg.RightChop(9);
			}
			else
			{
				if (!Folder.IsEmpty()) Folder += TEXT(" ");
				Folder += Arg;
			}
		}
		Folder = Folder.TrimStartAndEnd().TrimQuotes();   // tolerate stray outer quotes / padding if any survived

		const FQuestSourceColumns Cols = EnumerateForeignFileColumns(FormatName, Folder);
		if (!Cols.bReadable)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("EnumerateSourceColumns: %s"), *Cols.Error.ToString());
			return;
		}
		FString Joined;
		for (const FName& C : Cols.Columns) Joined += (Joined.IsEmpty() ? TEXT("") : TEXT(", ")) + C.ToString();
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("EnumerateSourceColumns: %d column(s)%s: %s"),
			Cols.Columns.Num(), Cols.bHasDuplicateColumns ? TEXT(" [DUPLICATE]") : TEXT(""), *Joined);
	}));

static FAutoConsoleCommand GExportQuestlineCmd(
	TEXT("SimpleQuest.ExportQuestline"),
	TEXT("Export a questline's authored model as the interlingua folder — per-type entity tables "
		"(reflection-driven, instanced sub-objects as child rows) + one knot-collapsed edge table. Args: the questline "
		"asset path, then optionally a destination folder — omit it for Saved/QuestExport/<QuestlineID>/. "
		"[--format=<name>] [--mapping=<asset>]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportQuestlineCmd));

