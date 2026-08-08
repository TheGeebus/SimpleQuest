// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// PROTOTYPE - Resolver, Phase 2 import (the round-trip's second half). Reconstructs AUTHORED editor nodes from the
// interlingua table folder an export produced, then feeds the EXISTING compiler - never reverses the compiler. Creates
// a FRESH asset (QuestlineID suffixed _RT so its compiled tag namespace doesn't collide with the original), so the
// round-trip is verifiable by the two oracles: C (re-export this asset, diff the folders modulo _RT) and B2
// (compile + DumpCompiled both, diff modulo the tag prefix). Console-triggered, editor-only. Not shipped API.

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/DataTable.h"
#include "Factories/QuestlineGraphFactory.h"
#include "IAssetTools.h"
#include "Internationalization/Text.h"
#include "ISimpleQuestEditorModule.h"
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
#include "Resolver/QuestRowRestore.h"
#include "SimpleQuestLog.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Utilities/QuestlineGraphCompiler.h"


namespace
{
	// A console-typed asset path is usually the short form "/Game/Path/Asset"; FSoftObjectPath needs "/Game/Path/Asset.Asset".
	FString NormalizeConsoleAssetPath(const FString& In)
	{
		if (In.Contains(TEXT("."))) return In;
		FString Ignored, AssetName;
		if (In.Split(TEXT("/"), &Ignored, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return In + TEXT(".") + AssetName;
		}
		return In;
	}

	const TCHAR* PlanActionName(EQuestNodePlanAction Action)
	{
		switch (Action)
		{
		case EQuestNodePlanAction::Create: return TEXT("CREATE");
		case EQuestNodePlanAction::Orphan: return TEXT("ORPHAN");
		default:                           return TEXT("UPDATE");
		}
	}

	void LogInPlacePlan(const FQuestInPlacePlan& Plan)
	{
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: in-place PLAN for '%s' - %d update(s) (%d with changes), %d create(s), %d orphan(s), "
			"%d node(s) outside the levels this source declares, %d contested key(s), %d wire edge(s) added, %d removed. Nothing was modified."),
			*Plan.TargetAssetPath,
			Plan.CountOf(EQuestNodePlanAction::Update),
			Plan.ChangedNodeCount(),
			Plan.CountOf(EQuestNodePlanAction::Create),
			Plan.CountOf(EQuestNodePlanAction::Orphan),
			Plan.UntouchedNodeCount,
			Plan.AmbiguousKeys.Num(),
			Plan.AddedEdges.Num(),
			Plan.RemovedEdges.Num());

		for (const FQuestDataEdge& E : Plan.RemovedEdges) { UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [WIRE-] %s|%s|%s"), *E.From, *E.Type, *E.To); }
		for (const FQuestDataEdge& E : Plan.AddedEdges)   { UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [WIRE+] %s|%s|%s"), *E.From, *E.Type, *E.To); }
		for (const FString& R : Plan.Refusals) { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("  [REFUSED] %s"), *R); }

		for (const FQuestNodePlanEntry& Entry : Plan.Entries)
		{
			// Unchanged matches are the common case on a healthy re-import; listing them would bury the ones that matter.
			if (Entry.Action == EQuestNodePlanAction::Update && Entry.Changes.Num() == 0 && !Entry.bMoved) continue;
			
			// An orphan has no incoming row, so its level is only known from the asset side; the questline itself sits in no
			// level at all, being the thing levels belong to.
			const FString& Level = (Entry.Action == EQuestNodePlanAction::Orphan) ? Entry.CurrentGraphCell : Entry.GraphCell;
			const FString Where = Entry.bIsQuestlineSelf ? FString(TEXT("the questline itself")) : FString::Printf(TEXT("graph '%s'"), *Level);
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [%s] %s (%s) - %s%s"),
				PlanActionName(Entry.Action),
				*Entry.Key,
				*Entry.ClassName,
				*Where,
				Entry.bMoved ? TEXT("  ** moves to a different container **") : TEXT(""));

			if (Entry.bMoved)
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("      graph: %s -> %s"), *Entry.CurrentGraphCell, *Entry.GraphCell);
			}
			for (const FQuestPropertyChange& Change : Entry.Changes)
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("      %s: '%s' -> '%s'"), *Change.Property, *Change.CurrentText, *Change.IncomingText);
			}
		}

		for (const FString& W : Plan.Warnings) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: %s"), *W);
		if (Plan.IsNoOp())
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: the asset already matches the source - a re-import would change nothing."));
		}
	}

	void ImportQuestlineCmd(const TArray<FString>& Args)
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
		if (!QuestImport_ReadAndValidate(Endpoint, LoadQuestMappingArg(Args), Bundle, NodeRowsByKey, AllRowKeys, Warnings, ReadError))
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: %s. No asset created."), *ReadError);
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
			Request.Mapping = LoadQuestMappingArg(Args);
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
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: %s. Nothing was modified."), *Outcome.Error);
				return;
			}

			Outcome.Plan.TargetAssetPath = AssetPath;
			LogInPlacePlan(Outcome.Plan);
			// The log is one rendering of the plan; the panel is another. Published unconditionally, including for a plan
			// about to be applied, so the panel always shows what the run actually decided.
			FQuestPlanSource PlanSource;
			PlanSource.Folder     = Endpoint.Folder;
			PlanSource.FormatName = Endpoint.FormatName;
			PlanSource.Table      = Endpoint.Table.ToSoftObjectPath();
			FQuestPlanBroker::Get().Publish(Outcome.Plan.TargetAssetPath, Outcome.Plan, PlanSource);

			if (!bApply)
			{
				return;   // planning is the default; mutating is opted into
			}

			// A plan carrying refusals or contested keys is not trustworthy in ANY part - those say the planner could not
			// describe the source, not merely that one row is odd. ApplyPlan already declined; this reports why.
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
				Result.NodesCreated,
				Result.NodesMoved,
				Result.EdgesChanged,
				Result.NodesDeleted,
				Result.EntriesDeferred,
				Result.Skipped.Num());

			// Only dirty the package if something actually happened. A re-import that changes nothing should leave no trace:
			// marking it regardless makes every no-op run look like a modification, which costs a save and a diff for work
			// that was not done - and trains a designer to ignore the one signal that says an asset moved.
			const bool bChangedAnything = Result.ChangedAnything();
			if (bChangedAnything)
			{
				TargetGraph->GetPackage()->MarkPackageDirty();
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
}

static FAutoConsoleCommand GImportQuestlineCmd(
	TEXT("SimpleQuest.ImportQuestline"),
	TEXT("PROTOTYPE: reconstruct a questline asset from an interlingua table folder (an ExportQuestline output) and "
		"compile it. Creates a fresh <QuestlineID>_RT asset. Args: <FolderPath> <DestPackagePath> (e.g. "
		"\"E:/.../Saved/QuestExport/QL_Ch1_BasicTrigger\" /Game/Imported)."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ImportQuestlineCmd));

static FAutoConsoleCommand GEnumerateSourceColumnsCmd(
	TEXT("SimpleQuest.EnumerateSourceColumns"),
	TEXT("PROTOTYPE: list the columns a foreign source exposes (proves the source-column provider seam). "
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
