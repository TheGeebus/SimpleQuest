// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


// PROTOTYPE — Resolver, Phase 2 adjacency export. Serializes a compiled questline ASSET's routing edges as a readable
// from/via/to triple-table to a per-questline text file, to evaluate the export format's intelligibility. Reads the
// compiled model directly off the asset (cold — no running game, no manager), which is the export's true source of truth.
// Read-only; console-triggered. Not shipped API.

#include "CoreMinimal.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	// Render the honest edge label from a runtime routing-id FName. A registered gameplay tag => static outcome (show
	// the leaf); otherwise a dynamic PathName. Mirrors the reward-manifest labeler's static/dynamic discriminator.
	FString RenderVia(FName PathIdentity)
	{
		if (PathIdentity.IsNone()) return TEXT("AnyOutcome");
		const FGameplayTag AsTag = UGameplayTagsManager::Get().RequestGameplayTag(PathIdentity, /*ErrorIfNotFound*/ false);
		if (AsTag.IsValid())
		{
			const FString Full = AsTag.ToString();
			int32 Dot;
			return FString::Printf(TEXT("Outcome:%s"), Full.FindLastChar(TEXT('.'), Dot) ? *Full.RightChop(Dot + 1) : *Full);
		}
		return FString::Printf(TEXT("Path:%s"), *PathIdentity.ToString());
	}

	// Shorten a full contextual tag to its readable trailing segments (full tags are long + repetitive). Keeps everything
	// after "SimpleQuest.Questline." so a designer reads "Ch1.Fight" not the whole namespace. Utility nodes carry
	// Util_<GUID> keys rather than contextual tags — passed through as-is so they're visibly distinct in the output.
	FString ShortTag(FName TagOrKey)
	{
		FString S = TagOrKey.ToString();
		const FString Prefix = TEXT("SimpleQuest.Questline.");
		return S.StartsWith(Prefix) ? S.RightChop(Prefix.Len()) : S;
	}

	void ExportRouting(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportRouting: usage 'SimpleQuest.ExportRouting <QuestlineAssetPath>' "
				"(e.g. /SimpleQuest/QuickStart/Chapters/01_BasicTrigger/QL_Ch1_BasicTrigger.QL_Ch1_BasicTrigger)."));
			return;
		}

		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportRouting: couldn't load questline asset '%s'."), *Args[0]);
			return;
		}

		TArray<FString> Rows;
		Rows.Add(TEXT("from\tvia\tto"));

		int32 EdgeCount = 0;
		for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : Graph->GetCompiledNodes())
		{
			const UQuestNodeBase* Node = Pair.Value;
			if (!Node) continue;

			// Key the FROM on the compiled-node map key (Pair.Key) — the node's own identity — rather than
			// GetContextualTag(), so utility nodes (which carry a Util_ key, not a contextual tag) render consistently.
			const FString From = ShortTag(Pair.Key);

			for (const TPair<FName, FQuestPathNodeList>& PathPair : Node->GetNextNodesByPath())
			{
				const FString Via = RenderVia(PathPair.Key);
				for (const FName& Dest : PathPair.Value.NodeTags)
				{
					Rows.Add(FString::Printf(TEXT("%s\t%s\t%s"), *From, *Via, *ShortTag(Dest)));
					++EdgeCount;
				}
			}
			for (const FName& Dest : Node->GetNextNodesOnAnyOutcome())
			{
				Rows.Add(FString::Printf(TEXT("%s\t%s\t%s"), *From, TEXT("AnyOutcome"), *ShortTag(Dest)));
				++EdgeCount;
			}
			// Forward edges — utility-node continuation (a reward/blocker/fact-setter forwards onward after its action,
			// not on an outcome). This is the util-node spine; without it, edges INTO utility nodes dead-end in the export.
			for (const FName& Dest : Node->GetNextNodesOnForward())
			{
				Rows.Add(FString::Printf(TEXT("%s\t%s\t%s"), *From, TEXT("Forward"), *ShortTag(Dest)));
				++EdgeCount;
			}
		}

		// Per-questline file, named by the questline's own ID — one asset, one file (the intended export grain).
		const FString OutDir = FPaths::ProjectSavedDir() / TEXT("QuestExport");
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
		const FString OutPath = FPaths::ConvertRelativePathToFull(OutDir / (Graph->GetQuestlineID() + TEXT("_routing.tsv")));
		if (FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")), *OutPath))
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("ExportRouting: '%s' — %d compiled node(s), wrote %d edge(s) to '%s'."),
				*Graph->GetQuestlineID(), 
				Graph->GetCompiledNodes().Num(), 
				EdgeCount, 
				*OutPath);
		}
		else
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportRouting: failed to write '%s'."), *OutPath);
		}
	}

		// Render one leaf's readable identity from its compiled fields, dispatched by Type (mirrors the evaluator's per-Type
	// field usage — see GetPrereqFieldContract). Shows what the leaf gates on: a fact, another node's outcome, etc.
	FString RenderLeaf(const FPrerequisiteExpressionNode& N)
	{
		switch (N.Type)
		{
		case EPrerequisiteExpressionType::Leaf:            // a raw WorldState fact
			return FString::Printf(TEXT("Fact:%s"), *ShortTag(N.LeafTag.GetTagName()));
		case EPrerequisiteExpressionType::Leaf_Path:       // a specific source node resolved via a specific path
			return FString::Printf(TEXT("%s Outcome:%s"), *ShortTag(N.LeafQuestTag.GetTagName()),
				*ShortTag(N.LeafPathIdentity));           // path identity is the outcome-tag-name or PathName
		case EPrerequisiteExpressionType::Leaf_Resolution: // (legacy) source node resolved with an outcome, any path
			return FString::Printf(TEXT("%s Resolved:%s"), *ShortTag(N.LeafQuestTag.GetTagName()),
				*ShortTag(N.LeafOutcomeTag.GetTagName()));
		case EPrerequisiteExpressionType::Leaf_Entry:      // source node entered with an incoming outcome
			return FString::Printf(TEXT("%s Entered:%s"), *ShortTag(N.LeafQuestTag.GetTagName()),
				*ShortTag(N.LeafOutcomeTag.GetTagName()));
		case EPrerequisiteExpressionType::Leaf_Outcome:    // ANY quest resolved with this outcome (context-free)
			return FString::Printf(TEXT("AnyQuest Outcome:%s"), *ShortTag(N.LeafOutcomeTag.GetTagName()));
		default:
			return TEXT("<?>");
		}
	}

	// Recursively render a compiled prereq expression subtree as a readable boolean string. Combinators wrap children;
	// leaves render their identity. Fully parenthesized so precedence is unambiguous when read.
	FString RenderPrereqNode(const FPrerequisiteExpression& Expr, int32 Index)
	{
		if (!Expr.Nodes.IsValidIndex(Index)) return TEXT("<invalid>");
		const FPrerequisiteExpressionNode& N = Expr.Nodes[Index];
		switch (N.Type)
		{
		case EPrerequisiteExpressionType::Always:
			return TEXT("Always");
		case EPrerequisiteExpressionType::And:
		case EPrerequisiteExpressionType::Or:
			{
				const TCHAR* Op = (N.Type == EPrerequisiteExpressionType::And) ? TEXT(" AND ") : TEXT(" OR ");
				TArray<FString> Parts;
				for (int32 Child : N.ChildIndices) Parts.Add(RenderPrereqNode(Expr, Child));
				return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, Op));
			}
		case EPrerequisiteExpressionType::Not:
			return FString::Printf(TEXT("NOT(%s)"),
				N.ChildIndices.Num() > 0 ? *RenderPrereqNode(Expr, N.ChildIndices[0]) : TEXT("?"));
		default:   // any leaf type
			return RenderLeaf(N);
		}
	}

	void ExportPrereqs(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportPrereqs: usage 'SimpleQuest.ExportPrereqs <QuestlineAssetPath>'."));
			return;
		}
		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportPrereqs: couldn't load questline asset '%s'."), *Args[0]);
			return;
		}

		TArray<FString> Rows;
		Rows.Add(TEXT("node\trequires"));

		int32 GatedCount = 0;
		for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : Graph->GetCompiledNodes())
		{
			const UQuestNodeBase* Node = Pair.Value;
			if (!Node) continue;
			const FPrerequisiteExpression& Expr = Node->GetPrerequisiteExpression();
			if (Expr.IsAlways()) continue;   // no prereq wired — skip (nothing to say)

			Rows.Add(FString::Printf(TEXT("%s\t%s"), *ShortTag(Pair.Key), *RenderPrereqNode(Expr, Expr.RootIndex)));
			++GatedCount;
		}

		const FString OutDir = FPaths::ProjectSavedDir() / TEXT("QuestExport");
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
		const FString OutPath = FPaths::ConvertRelativePathToFull(OutDir / (Graph->GetQuestlineID() + TEXT("_prereqs.tsv")));
		if (FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")), *OutPath))
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("ExportPrereqs: '%s' — %d gated node(s), wrote '%s'."),
				*Graph->GetQuestlineID(), GatedCount, *OutPath);
		}
		else
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportPrereqs: failed to write '%s'."), *OutPath);
		}
	}
}

static FAutoConsoleCommand GExportRoutingCmd(
	TEXT("SimpleQuest.ExportRouting"),
	TEXT("PROTOTYPE: serialize a compiled questline asset's routing as a from/via/to table to "
		"Saved/QuestExport/<QuestlineID>_routing.tsv. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportRouting));

static FAutoConsoleCommand GExportPrereqsCmd(
	TEXT("SimpleQuest.ExportPrereqs"),
	TEXT("PROTOTYPE: serialize a compiled questline asset's prerequisite gates as readable node/requires expressions "
		"to Saved/QuestExport/<QuestlineID>_prereqs.tsv. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportPrereqs));
