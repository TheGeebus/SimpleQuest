// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "QuestImportOperations.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/QuestlineGraphFactory.h"
#include "IAssetTools.h"
#include "ISimpleQuestEditorModule.h"
#include "Misc/PackageName.h"
#include "QuestBundleTransforms.h"
#include "QuestFlowConventions.h"
#include "QuestGraphBuilder.h"
#include "QuestInPlaceApply.h"
#include "QuestInPlacePlanner.h"
#include "QuestInstancedChildren.h"
#include "QuestRowRestore.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestImportMapping.h"
#include "UObject/SavePackage.h"
#include "Utilities/QuestlineGraphCompiler.h"

bool QuestImport_ValidateBundle(const UQuestImportMapping* Mapping,
	FQuestDataBundle& OutBundle,
	TMap<FString, const FQuestDataRow*>& OutNodeRowsByKey,
	TSet<FString>& OutAllRowKeys,
	TArray<FString>& OutWarnings,
	FString& OutError)
{
	// Everything the read pipeline does AFTER a bundle exists. Split out so a caller holding a bundle already - one
	// given files in memory rather than a folder - runs the identical mapping, wire-binding, convention and validation
	// path rather than a parallel one that can drift from it.

	// A studio's source-shape translation, before the flow conventions so a mapped column can feed one.
	if (Mapping)
	{
		if (!QuestBundle_ApplyMapping(OutBundle, *Mapping, OutWarnings))
		{
			// Phrased as the caller will print it. The three failure modes here produce three different messages today,
			// and collapsing them into one call must not collapse the messages - a reader distinguishes "could not read"
			// from "refused the shape" from "read but incoherent" by exactly this wording.
			OutError = TEXT("mapping guard refused the import");
			return false;
		}
		QuestBundle_ApplyWireBindings(OutBundle, *Mapping, OutWarnings);
	}
	ApplyQuestFlowConventions(OutBundle, OutWarnings);

	FString ValidateError;
	if (!QuestBundle_Validate(OutBundle, OutNodeRowsByKey, OutAllRowKeys, ValidateError))
	{
		OutError = FString::Printf(TEXT("validation failed - %s"), *ValidateError);
		return false;
	}
	return true;
}

bool QuestImport_ReadAndValidate(const FQuestDataEndpoint& Endpoint,
	const UQuestImportMapping* Mapping,
	FQuestDataBundle& OutBundle,
	TMap<FString, const FQuestDataRow*>& OutNodeRowsByKey,
	TSet<FString>& OutAllRowKeys,
	TArray<FString>& OutWarnings,
	FString& OutError)
{
	if (!ReadEndpointBundle(Endpoint, OutBundle, OutError)) { return false; }
	return QuestImport_ValidateBundle(Mapping, OutBundle, OutNodeRowsByKey, OutAllRowKeys, OutWarnings, OutError);
}

FQuestAbsentPolicyResolver QuestImport_ResolvePolicies(const UQuestImportMapping* Mapping, bool bResetAbsent)
{
	FQuestAbsentPolicyResolver Policies;
	if (Mapping)
	{
		Policies.Default = Mapping->DefaultAbsentPolicy;
		for (const FQuestColumnBinding& B : Mapping->Bindings)
		{
			if (!B.TargetProperty.IsNone()) { Policies.ByProperty.Add(B.TargetProperty, B.AbsentPolicy); }
		}
	}
	// A RUN-LEVEL instruction has to reach the per-property entries, not just the fallback: a recipe writes an explicit
	// entry per bound column, and a per-binding Preserve would otherwise shadow the flag for exactly the columns the
	// recipe covers. Require is left alone - that is a designer asserting a value must be present.
	if (bResetAbsent)
	{
		if (Policies.Default == EQuestAbsentFieldPolicy::Preserve) { Policies.Default = EQuestAbsentFieldPolicy::Reset; }
		for (TPair<FName, EQuestAbsentFieldPolicy>& Entry : Policies.ByProperty)
		{
			if (Entry.Value == EQuestAbsentFieldPolicy::Preserve) { Entry.Value = EQuestAbsentFieldPolicy::Reset; }
		}
	}
	return Policies;
}

bool QuestImport_RunInPlace(UQuestlineGraph& Target, const FQuestImportRequest& Request, bool bApply, FQuestImportOutcome& Out)
{
	FQuestDataBundle Bundle;
	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	TSet<FString> AllRowKeys;
	if (!QuestImport_ReadAndValidate(Request.Endpoint, Request.Mapping, Bundle, NodeRowsByKey, AllRowKeys, Out.Warnings, Out.Error))
	{
		return false;
	}

	return QuestImport_RunInPlaceFromBundle(Target, Bundle, NodeRowsByKey, AllRowKeys, Request.Mapping, Request.Policies, Request.bDeleteOrphans, bApply, Out);
}

bool QuestImport_RunInPlaceFromBundle(UQuestlineGraph& Target,
	FQuestDataBundle& Bundle,
	const TMap<FString, const FQuestDataRow*>& NodeRowsByKey,
	const TSet<FString>& AllRowKeys,
	const UQuestImportMapping* Mapping,
	const FQuestAbsentPolicyResolver& Policies,
	bool bDeleteOrphans,
	bool bApply,
	FQuestImportOutcome& Out)
{
	// The whole in-place operation from a bundle that is already read and validated. RunInPlace above is now this plus
	// the read, so a files-in caller and a folder-in caller execute the identical planning and apply path - which is
	// the only way "what was reviewed is what executes" survives having two entry points.

	PlanQuestInPlace(Target, Bundle, NodeRowsByKey, Out.Warnings, Out.Plan, Policies);
	Out.bPlanned = true;

	// A plan EXISTS now, so the warnings that produced it belong ON it. They are collected separately because a read can
	// fail before there is any plan to hold them - but once there is one, two lists means every consumer has to remember
	// to merge, and all three forgot: the panel showed none of them, and the console and commandlet render only what the
	// plan carries. MOVED rather than copied, so nothing downstream can report them twice.
	Out.Plan.Warnings.Append(Out.Warnings);
	Out.Warnings.Reset();

	if (!bApply) { return true; }

	FQuestApplyOptions Options;
	if (Mapping) { Options.bDeleteOrphanedNodes = Mapping->bDeleteOrphanedNodes; }
	if (bDeleteOrphans) { Options.bDeleteOrphanedNodes = true; }

	ApplyQuestPlan(Target, Out.Plan, Bundle, NodeRowsByKey, Out.ApplyResult, Options);

	// An apply that changed the graph leaves the COMPILED model describing the graph as it was. Everything downstream
	// reads that model - the runtime, the state subsystem, the tag registry - so the asset would be quietly
	// inconsistent with itself until something else triggered a recompile. Skipped when nothing changed, because the
	// compiled model is already correct and a needless compile costs a tag-tree rebuild.
	if (Out.ApplyResult.ChangedAnything())
	{
		Out.ApplyResult.bCompileSucceeded =	ISimpleQuestEditorModule::Get().CompileQuestlineAndNeighborhood(&Target, Out.ApplyResult.GraphsCompiled);
	}
	
	Out.bApplied = !Out.ApplyResult.bRefused;
	return true;
}

bool QuestImport_CreateFromBundle(const FQuestDataBundle& Bundle,
	const TMap<FString, const FQuestDataRow*>& NodeRowsByKey,
	const FString& DestPackagePath,
	const FString& AssetNameSuffix,
	FQuestCreateOutcome& Out,
	TArray<FString>& OutWarnings,
	FString& OutError)
{
	const FQuestDataTable* SelfTable = Bundle.TablesByType.Find(TEXT("questline_graph"));
	if (!SelfTable || SelfTable->Rows.Num() == 0)
	{
		OutError = TEXT("the source carries no questline_graph row, so there is nothing to create. Nothing was created.");
		return false;
	}

	// P1 - create the asset via the factory, then restore the self row (with the suffixed identity + instanced rewards).
	// Two distinct identities: the ROW KEY (sanitized EffectiveID - folder name, tag namespace) and the authored
	// QuestlineID FIELD (raw, whatever the designer typed, spaces and all - the compiler sanitizes it only when
	// building tags, never mutating the field). The asset NAME rides the sanitized key (a package name can't hold
	// spaces); the QuestlineID FIELD must preserve the raw authored value so the round-trip doesn't alter it.
	const FQuestDataRow& SelfRow = SelfTable->Rows[0];
	const FString OriginalKey = SelfRow.Key;                          // sanitized - folder/tag identity
	const FString RawQuestlineID = SelfRow.Get(TEXT("QuestlineID"));  // raw authored field (may be empty)
	const FString AssetName = OriginalKey + AssetNameSuffix;

	// REFUSE rather than overwrite, and refuse rather than let CreateAsset quietly mint a numbered variant - silently
	// producing 'QL_Chapter1_2' is worse than stopping, because nothing downstream would ever name the collision.
	const FString FullPackagePath = DestPackagePath / AssetName;
	if (FPackageName::DoesPackageExist(FullPackagePath))
	{
		OutError = FString::Printf(TEXT("'%s' already exists. This creates a NEW questline and will not overwrite one - "
			"plan and apply are the path for updating an existing asset. Nothing was created."), *FullPackagePath);
		return false;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UQuestlineGraphFactory* Factory = NewObject<UQuestlineGraphFactory>();
	UObject* Created = AssetTools.CreateAsset(AssetName, DestPackagePath, UQuestlineGraph::StaticClass(), Factory);
	UQuestlineGraph* Graph = Cast<UQuestlineGraph>(Created);
	if (!Graph || !Graph->QuestlineEdGraph)
	{
		OutError = FString::Printf(TEXT("asset creation failed at '%s/%s'. Nothing was created."), *DestPackagePath, *AssetName);
		return false;
	}

	TSet<FString> Consumed;

	// Self-row properties onto the graph object (QuestlineID gets the suffix; instanced QuestlineRewards rebuilt).
	RestoreQuestRowProperties(Graph, SelfRow);
	{
		// QuestlineID handling. Two cases, because GetEffectiveID() falls back to the ASSET NAME when the field is empty:
		//   - Source field NON-empty: set the field to <raw><suffix>, so a re-export's QuestlineID cell matches the
		//     source's modulo the suffix.
		//   - Source field EMPTY (asset-name-derived): LEAVE IT EMPTY. The source's EffectiveID was its asset name, and
		//     this asset's name already carries the suffix, so the same empty->asset-name fallback yields the suffixed
		//     name - matching the source modulo the suffix. Writing the bare suffix here (the prior bug) would make
		//     QuestlineID = "_RT", tags = SimpleQuest.Questline._RT.*, and the export folder "_RT" - diverging from the
		//     asset-name identity the source actually used. With an EMPTY suffix both branches are already correct.
		if (!RawQuestlineID.IsEmpty())
		{
			if (FProperty* IDProp = Graph->GetClass()->FindPropertyByName(TEXT("QuestlineID")))
			{
				const FString Suffixed = RawQuestlineID + AssetNameSuffix;
				IDProp->ImportText_Direct(*Suffixed, IDProp->ContainerPtrToValuePtr<void>(Graph), nullptr, PPF_None);
			}
		}
		// else: RestoreQuestRowProperties already left it empty (the source cell was empty) - nothing to do.
	}
	ReattachQuestInstancedChildren(Graph, OriginalKey, Bundle, Consumed, OutWarnings);   // self-row child keys are prefixed by the self key

	// P2 - spawn nodes, root graph first, recursing into container inner graphs.
	TMap<FString, UEdGraphNode*> NodeByKey;
	ImportQuestGraphLevel(Graph->QuestlineEdGraph, TEXT("root"), Bundle, NodeRowsByKey, NodeByKey, Consumed, OutWarnings);

	// P3 - pin refresh pass (innermost-first).
	RefreshQuestNodePins(Bundle, NodeRowsByKey, NodeByKey, OutWarnings);

	// P4 - wire edges + contains-edge cross-check.
	WireQuestEdges(Bundle, NodeByKey, Consumed, OutWarnings);

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

	// A failed compile is a WARNING rather than a refusal: the asset exists and is saved by this point, so failing the
	// call would report "nothing created" about something that was. The caller decides how loudly to say it.
	if (!bCompiled)
	{
		OutWarnings.Add(FString::Printf(TEXT("'%s' was created and saved, but its compile FAILED."), *AssetName));
	}

	Out.Graph = Graph;
	Out.AssetName = AssetName;
	Out.AssetPath = Package->GetName();
	Out.NodeCount = NodeByKey.Num();
	Out.bCompiled = bCompiled;
	return true;
}

