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
#include "GameplayTagContainer.h"
#include "Rewards/QuestRewardBase.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	// Escape embedded whitespace exactly as the authored export does, so dump lines stay one-per-record.
	FString DumpSanitize(const FString& In)
	{
		return In.Replace(TEXT("\t"), TEXT("\\t")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("\\n"));
	}

	// Serialize one property value. Sets and maps get SORTED element-wise renders (TSet/TMap iteration order is
	// insertion-dependent — raw ExportTextItem would leak authoring order into the dump and break determinism).
	// Everything else goes through ExportTextItem directly.
	FString DumpProperty(const FProperty* Prop, const void* ValuePtr)
	{
		// FText renders SOURCE-STRING-ONLY: the NSLOCTEXT namespace embeds the package GUID, which legitimately
		// differs between an original and its round-tripped twin — loc identity is asset identity, not behavior.
		if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			return TextProp->GetPropertyValue(ValuePtr).ToString();
		}
		if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			TArray<FString> Elems;
			FScriptSetHelper Helper(SetProp, ValuePtr);
			for (FScriptSetHelper::FIterator It(Helper); It; ++It)
			{
				FString Elem;
				SetProp->ElementProp->ExportTextItem_Direct(Elem, Helper.GetElementPtr(It), nullptr, nullptr, PPF_None);
				Elems.Add(Elem);
			}
			Elems.Sort();
			return FString::Printf(TEXT("{%s}"), *FString::Join(Elems, TEXT(",")));
		}
		if (const FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			TArray<FString> Pairs;
			FScriptMapHelper Helper(MapProp, ValuePtr);
			for (FScriptMapHelper::FIterator It(Helper); It; ++It)
			{
				FString K, V;
				MapProp->KeyProp->ExportTextItem_Direct(K, Helper.GetKeyPtr(It), nullptr, nullptr, PPF_None);
				MapProp->ValueProp->ExportTextItem_Direct(V, Helper.GetValuePtr(It), nullptr, nullptr, PPF_None);
				Pairs.Add(FString::Printf(TEXT("%s=%s"), *K, *V));
			}
			Pairs.Sort();
			return FString::Printf(TEXT("{%s}"), *FString::Join(Pairs, TEXT(",")));
		}
		// Sort element-wise for compiled TArray fields whose order is COLLECTION order, not semantics — container
		// reachability/membership lists (a fresh import collects them in a different node-spawn order). Targeted by
		// name because array order IS meaningful for other fields (e.g. reward grant sequence) — reflection can't
		// distinguish "ordered" from "unordered" arrays, so the dump names the unordered ones explicitly.
		static const TSet<FName> UnorderedArrayFields = {
			TEXT("EntryStepTags"), TEXT("InnerStepTags"), TEXT("EntryNodeTags")
		};
		if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			if (UnorderedArrayFields.Contains(Prop->GetFName()))
			{
				TArray<FString> Elems;
				FScriptArrayHelper Helper(ArrProp, ValuePtr);
				for (int32 i = 0; i < Helper.Num(); ++i)
				{
					FString Elem;
					ArrProp->Inner->ExportTextItem_Direct(Elem, Helper.GetRawPtr(i), nullptr, nullptr, PPF_None);
					Elems.Add(Elem);
				}
				Elems.Sort();
				return FString::Printf(TEXT("[%s]"), *FString::Join(Elems, TEXT(",")));
			}
		}
		FString Out;
		Prop->ExportTextItem_Direct(Out, ValuePtr, nullptr, nullptr, PPF_None);
		return Out;
	}

	// Render a prereq expression as a normalized nested string: combinator children are rendered recursively then
	// SORTED before joining (AND/OR are commutative — confirmed in CompileCombinatorNode, which appends children in
	// pin-encounter order, i.e. authoring order; the dump must not leak that). Leaves render their full field set.
	FString RenderPrereqNode(const FPrerequisiteExpression& Expr, int32 NodeIndex);

	FString RenderPrereqChildren(const FPrerequisiteExpression& Expr, const TArray<int32>& ChildIndices)
	{
		TArray<FString> Rendered;
		for (const int32 Child : ChildIndices)
		{
			Rendered.Add(RenderPrereqNode(Expr, Child));
		}
		Rendered.Sort();
		return FString::Join(Rendered, TEXT(","));
	}

	FString RenderPrereqNode(const FPrerequisiteExpression& Expr, int32 NodeIndex)
	{
		if (!Expr.Nodes.IsValidIndex(NodeIndex)) return TEXT("<invalid>");
		const FPrerequisiteExpressionNode& Node = Expr.Nodes[NodeIndex];

		// Combinators: TYPE(sorted children). Leaves: TYPE(field=value,...) — every leaf field rendered so the
		// per-Type field-role contract's honest fields all participate in the diff.
		switch (Node.Type)
		{
		case EPrerequisiteExpressionType::And:
			return FString::Printf(TEXT("AND(%s)"), *RenderPrereqChildren(Expr, Node.ChildIndices));
		case EPrerequisiteExpressionType::Or:
			return FString::Printf(TEXT("OR(%s)"), *RenderPrereqChildren(Expr, Node.ChildIndices));
		case EPrerequisiteExpressionType::Not:
			return FString::Printf(TEXT("NOT(%s)"), *RenderPrereqChildren(Expr, Node.ChildIndices));
		default:
		{
			// Leaf: reflection-walk the node struct's fields (skip Type + ChildIndices) so any future leaf field
			// automatically joins the dump. Deterministic: reflection order is declaration order.
			TArray<FString> Fields;
			const UScriptStruct* Struct = FPrerequisiteExpressionNode::StaticStruct();
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				const FString Name = It->GetName();
				if (Name == TEXT("Type") || Name == TEXT("ChildIndices")) continue;
				FString Val;
				It->ExportTextItem_Direct(Val, It->ContainerPtrToValuePtr<void>(&Node), nullptr, nullptr, PPF_None);
				Fields.Add(FString::Printf(TEXT("%s=%s"), *Name, *Val));
			}
			return FString::Printf(TEXT("%s(%s)"), *UEnum::GetValueAsString(Node.Type), *FString::Join(Fields, TEXT(",")));
		}
		}
	}

		// Render a reward instance as Class(prop=val,...) via reflection — the compiled questline-rewards map holds
	// Instanced UQuestRewardBase subobjects whose raw ExportTextItem form is a per-asset object PATH, which would
	// differ between two behaviorally-identical assets (X vs the round-tripped X'). Rendering the reward's own
	// EditAnywhere non-Transient properties compares the CONFIG, which is what behavioral identity means here.
	FString RenderRewardInstance(const UQuestRewardBase* Reward)
	{
		if (!Reward) return TEXT("<null>");
		TArray<FString> Fields;
		for (TFieldIterator<FProperty> It(Reward->GetClass()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_Transient)) continue;
			FString Val;
			It->ExportTextItem_Direct(Val, It->ContainerPtrToValuePtr<void>(Reward), nullptr, nullptr, PPF_None);
			Fields.Add(FString::Printf(TEXT("%s=%s"), *It->GetName(), *Val));
		}
		return FString::Printf(TEXT("%s(%s)"), *Reward->GetClass()->GetName(), *FString::Join(Fields, TEXT(",")));
	}

	// Render the compiled questline-rewards map: identity -> outcome -> ordered reward-config renders. Map levels
	// sorted; the reward ARRAY keeps authored order (grant order is a real, designer-visible sequence — the one
	// compiled ordering that is semantics, not noise).
	FString RenderCompiledQuestlineRewards(const UQuestlineGraph* Graph)
	{
		// Reflection-read the private map (access-blind, same as every other walk in this file).
		const FMapProperty* MapProp = CastField<FMapProperty>(Graph->GetClass()->FindPropertyByName(TEXT("CompiledQuestlineRewards")));
		if (!MapProp) return TEXT("<missing-property>");

		TArray<FString> IdentityEntries;
		FScriptMapHelper IdentityHelper(MapProp, MapProp->ContainerPtrToValuePtr<void>(Graph));
		for (FScriptMapHelper::FIterator It(IdentityHelper); It; ++It)
		{
			const FName IdentityKey = *reinterpret_cast<const FName*>(IdentityHelper.GetKeyPtr(It));
			const FQuestCompiledQuestlineRewards* Value = reinterpret_cast<const FQuestCompiledQuestlineRewards*>(IdentityHelper.GetValuePtr(It));

			TArray<FString> OutcomeEntries;
			for (const TPair<FGameplayTag, FQuestRewardSet>& OutcomePair : Value->RewardsByOutcome)
			{
				TArray<FString> RewardRenders;
				for (const TObjectPtr<UQuestRewardBase>& Reward : OutcomePair.Value.Rewards)
				{
					RewardRenders.Add(RenderRewardInstance(Reward));
				}
				OutcomeEntries.Add(FString::Printf(TEXT("%s=[%s]"), *OutcomePair.Key.ToString(), *FString::Join(RewardRenders, TEXT(","))));
			}
			OutcomeEntries.Sort();
			IdentityEntries.Add(FString::Printf(TEXT("%s:{%s}"), *IdentityKey.ToString(), *FString::Join(OutcomeEntries, TEXT(","))));
		}
		IdentityEntries.Sort();
		return FString::Join(IdentityEntries, TEXT(";"));
	}

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

		TArray<FString> Lines;

		// Graph-level pass: reflection over the graph object's own UPROPERTYs, restricted to the COMPILER-STAMPED
		// population. The graph class structurally mixes three populations — compiler-stamped (this dump's domain),
		// authored config (the EXPORT's domain: QuestlineID/DisplayName/QuestlineRewards/etc. — B2 judges behavior,
		// C judges authorship), and editor-only (QuestlineEdGraph) — and no property flag separates the first two,
		// so the filter is an explicit name allowlist. CompiledNodes is excluded here because it gets the richer
		// per-node dump below (its raw ExportTextItem form would be meaningless object paths). Unordered containers
		// render sorted via DumpProperty; sorted arrays are handled per-property where order is insertion noise.
		{
			// PendingTagRenames deliberately absent: it's rename HISTORY, not behavior — a fresh round-tripped asset
			// has an empty ledger, so including it would guarantee a benign diff on every asset with authoring history.
			static const TSet<FName> CompilerStampedGraphProps = {
				TEXT("CompiledQuestTags"), TEXT("EntryNodeTags"), TEXT("CompiledNodeAliases"),
				TEXT("CompiledQuestlineRewards"), TEXT("ListenerGroupTags"), TEXT("OutwardSetterGroupTags")
			};
			for (TFieldIterator<FProperty> It(Graph->GetClass()); It; ++It)
			{
				const FProperty* Prop = *It;
				if (!CompilerStampedGraphProps.Contains(Prop->GetFName())) continue;
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Graph);
				FString Rendered;
				if (Prop->GetFName() == TEXT("CompiledQuestlineRewards"))
				{
					Rendered = RenderCompiledQuestlineRewards(Graph);
				}
				else if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
				{
					// Compiled arrays' element order is compile-walk order (insertion noise, not semantics) — render
					// element-wise and sort, same normalization discipline as sets.
					TArray<FString> Elems;
					FScriptArrayHelper Helper(ArrProp, ValuePtr);
					for (int32 i = 0; i < Helper.Num(); ++i)
					{
						FString Elem;
						ArrProp->Inner->ExportTextItem_Direct(Elem, Helper.GetRawPtr(i), nullptr, nullptr, PPF_None);
						Elems.Add(Elem);
					}
					Elems.Sort();
					Rendered = FString::Printf(TEXT("{%s}"), *FString::Join(Elems, TEXT(",")));
				}
				else
				{
					Rendered = DumpProperty(Prop, ValuePtr);
				}
				Lines.Add(FString::Printf(TEXT("GRAPH\t%s\t%s"), *Prop->GetName(), *DumpSanitize(Rendered)));
			}
		}

		// Per-node reflection dump: one line per (node tag, property), tags sorted, properties in declaration order.
		// Every UPROPERTY on the compiled node participates — future compiled fields join automatically. The prereq
		// expression gets the normalized bespoke render instead of its raw reflected form (child-order + leaf clarity).
		TArray<FName> NodeTags;
		Graph->GetCompiledNodes().GetKeys(NodeTags);
		NodeTags.Sort(FNameLexicalLess());
		for (const FName& Tag : NodeTags)
		{
			const UQuestNodeBase* Node = Graph->GetCompiledNodes()[Tag];
			if (!Node) { Lines.Add(FString::Printf(TEXT("%s\t<null>"), *Tag.ToString())); continue; }

			Lines.Add(FString::Printf(TEXT("%s\tClass\t%s"), *Tag.ToString(), *Node->GetClass()->GetName()));
			// NodeInfo repackages ContextualTag + DisplayName (both dumped as their own lines) and embeds FText whose
			// NSLOCTEXT namespace is per-package — skip the duplicate. CachedGameInstance is a runtime cache pointer.
			static const TSet<FName> NodeDumpSkips = { TEXT("NodeInfo"), TEXT("CachedGameInstance") };
			for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
			{
				const FProperty* Prop = *It;
				if (Prop->HasAnyPropertyFlags(CPF_Transient)) continue;
				if (NodeDumpSkips.Contains(Prop->GetFName())) continue;
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Node);
				FString Rendered;
				if (Prop->GetName() == TEXT("PrerequisiteExpression"))
				{
					const FPrerequisiteExpression& Expr = Node->GetPrerequisiteExpression();
					Rendered = Expr.Nodes.IsEmpty() ? TEXT("<none>") : RenderPrereqNode(Expr, 0);
				}
				else
				{
					Rendered = DumpProperty(Prop, ValuePtr);
				}
				Lines.Add(FString::Printf(TEXT("%s\t%s\t%s"), *Tag.ToString(), *Prop->GetName(), *DumpSanitize(Rendered)));
			}
		}

		const FString OutDir = FPaths::ProjectSavedDir() / TEXT("QuestExport");
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
		const FString QLID = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Graph->GetEffectiveID());
		const FString OutPath = FPaths::ConvertRelativePathToFull(OutDir / (QLID + TEXT("_compiled_dump.tsv")));
		if (FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutPath))
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("DumpCompiled: '%s' — %d line(s), %d node(s). Wrote '%s'."),
				*QLID, Lines.Num(), NodeTags.Num(), *OutPath);
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