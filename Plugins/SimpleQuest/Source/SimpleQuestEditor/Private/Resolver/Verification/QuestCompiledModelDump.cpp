// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/Verification/QuestCompiledModelDump.h"

#include "GameplayTagContainer.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Rewards/QuestRewardBase.h"
#include "UObject/UnrealType.h"


/** Escape embedded whitespace exactly as the authored export does, so dump lines stay one-per-record. */
static FString DumpSanitize(const FString& In)
{
	return In.Replace(TEXT("\t"), TEXT("\\t")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("\\n"));
}

/**
 * Serialize one property value. Sets and maps get SORTED element-wise renders (TSet/TMap iteration order is
 * insertion-dependent — raw ExportTextItem would leak authoring order into the dump and break determinism).
 * Structs render member-wise through this same function so the FText policy reaches nested values; everything else
 * goes through ExportTextItem directly.
 */
static FString DumpProperty(const FProperty* Prop, const void* ValuePtr)
{
	// FText renders SOURCE-STRING-ONLY: the NSLOCTEXT namespace embeds the package GUID, which legitimately
	// differs between an original and its round-tripped twin — loc identity is asset identity, not behavior.
	if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
	{
		return TextProp->GetPropertyValue(ValuePtr).ToString();
	}

	// Structs render MEMBER-WISE through this same function, so the FText policy above reaches values NESTED inside a
	// struct. Handing a struct to ExportTextItem instead re-exports its members with PPF_Delimited, which restores the
	// loc-identity form this dump exists to strip - and a nested FText was the one case that slipped past the check
	// above. It surfaced as a save-order-dependent diff: the imported asset gets saved, and serialization mints a text
	// key for every keyless FText, while the source is compiled in memory and never serialized at all.
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		TArray<FString> Members;
		for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
		{
			Members.Add(FString::Printf(TEXT("%s=%s"), *It->GetName(), *DumpProperty(*It, It->ContainerPtrToValuePtr<void>(ValuePtr))));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Members, TEXT(",")));
	}
	
	if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
	{
		TArray<FString> Elems;
		FScriptSetHelper Helper(SetProp, ValuePtr);
		for (FScriptSetHelper::FIterator It(Helper); It; ++It)
		{
			Elems.Add(DumpProperty(SetProp->ElementProp, Helper.GetElementPtr(It)));
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
			Pairs.Add(FString::Printf(TEXT("%s=%s"),
				*DumpProperty(MapProp->KeyProp, Helper.GetKeyPtr(It)),
				*DumpProperty(MapProp->ValueProp, Helper.GetValuePtr(It))));
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
				Elems.Add(DumpProperty(ArrProp->Inner, Helper.GetRawPtr(i)));
			}
			Elems.Sort();
			return FString::Printf(TEXT("[%s]"), *FString::Join(Elems, TEXT(",")));
		}
	}
	FString Out;
	Prop->ExportTextItem_Direct(Out, ValuePtr, nullptr, nullptr, PPF_None);
	return Out;
}

/**
 * Render a prereq expression as a normalized nested string: combinator children are rendered recursively then
 * SORTED before joining (AND/OR are commutative — confirmed in CompileCombinatorNode, which appends children in
 * pin-encounter order, i.e. authoring order; the dump must not leak that). Leaves render their full field set.
 */
static FString RenderPrereqNode(const FPrerequisiteExpression& Expr, int32 NodeIndex);

static FString RenderPrereqChildren(const FPrerequisiteExpression& Expr, const TArray<int32>& ChildIndices)
{
	TArray<FString> Rendered;
	for (const int32 Child : ChildIndices)
	{
		Rendered.Add(RenderPrereqNode(Expr, Child));
	}
	Rendered.Sort();
	return FString::Join(Rendered, TEXT(","));
}

static FString RenderPrereqNode(const FPrerequisiteExpression& Expr, int32 NodeIndex)
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
			const FString Val = DumpProperty(*It, It->ContainerPtrToValuePtr<void>(&Node));
			Fields.Add(FString::Printf(TEXT("%s=%s"), *Name, *Val));
		}
		return FString::Printf(TEXT("%s(%s)"), *UEnum::GetValueAsString(Node.Type), *FString::Join(Fields, TEXT(",")));
	}
	}
}

/**
 * Render a reward instance as Class(prop=val,...) via reflection — the compiled questline-rewards map holds
 * Instanced UQuestRewardBase subobjects whose raw ExportTextItem form is a per-asset object PATH, which would
 * differ between two behaviorally-identical assets (X vs the round-tripped X'). Rendering the reward's own
 * EditAnywhere non-Transient properties compares the CONFIG, which is what behavioral identity means here.
 */
static FString RenderRewardInstance(const UQuestRewardBase* Reward)
{
	if (!Reward) return TEXT("<null>");
	TArray<FString> Fields;
	for (TFieldIterator<FProperty> It(Reward->GetClass()); It; ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_Transient)) continue;
		const FString Val = DumpProperty(*It, It->ContainerPtrToValuePtr<void>(Reward));
		Fields.Add(FString::Printf(TEXT("%s=%s"), *It->GetName(), *Val));
	}
	return FString::Printf(TEXT("%s(%s)"), *Reward->GetClass()->GetName(), *FString::Join(Fields, TEXT(",")));
}

/**
 * Render the compiled questline-rewards map: identity -> outcome -> ordered reward-config renders. Map levels
 * sorted; the reward ARRAY keeps authored order (grant order is a real, designer-visible sequence — the one
 * compiled ordering that is semantics, not noise).
 */
static FString RenderCompiledQuestlineRewards(const UQuestlineGraph* Graph)
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

TArray<FString> RenderQuestCompiledModel(const UQuestlineGraph& Graph, int32& OutNodeCount)
{
	TArray<FString> Lines;

	// Graph-level pass: reflection over the graph object's own UPROPERTYs, restricted to the COMPILER-STAMPED
	// population. The graph class structurally mixes three populations — compiler-stamped (this dump's domain),
	// authored config (the EXPORT's domain: QuestlineID/DisplayName/QuestlineRewards/etc. — this dump judges
	// behaviour, the export comparison judges authorship), and editor-only (QuestlineEdGraph) — and no property
	// flag separates the first two, so the filter is an explicit name allowlist. CompiledNodes is excluded here
	// because it gets the richer per-node dump below (its raw ExportTextItem form would be meaningless object paths).
	// Unordered containers render sorted via DumpProperty; sorted arrays are handled per-property where order is
	// insertion noise.
	{
		static const TSet<FName> CompilerStampedGraphProps = {
			TEXT("CompiledQuestTags"), TEXT("EntryNodeTags"), TEXT("CompiledNodeAliases"),
			TEXT("CompiledQuestlineRewards"), TEXT("ListenerGroupTags"), TEXT("OutwardSetterGroupTags")
		};
		for (TFieldIterator<FProperty> It(Graph.GetClass()); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!CompilerStampedGraphProps.Contains(Prop->GetFName())) continue;
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(&Graph);
			FString Rendered;
			if (Prop->GetFName() == TEXT("CompiledQuestlineRewards"))
			{
				Rendered = RenderCompiledQuestlineRewards(&Graph);
			}
			else if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
			{
				// Compiled arrays' element order is compile-walk order (insertion noise, not semantics) — render
				// element-wise and sort, same normalization discipline as sets.
				TArray<FString> Elems;
				FScriptArrayHelper Helper(ArrProp, ValuePtr);
				for (int32 i = 0; i < Helper.Num(); ++i)
				{
					const FString Elem = DumpProperty(ArrProp->Inner, Helper.GetRawPtr(i));
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
	Graph.GetCompiledNodes().GetKeys(NodeTags);
	NodeTags.Sort(FNameLexicalLess());

	// The node COUNT as a graph-level line. This render DELIBERATELY destroys incidental identity - loc namespaces,
	// container order, object paths - which is what makes two behaviourally-identical assets produce identical text,
	// and also what makes it lossy. A count is the one non-lossy invariant over it: two dumps whose blocks all
	// normalize alike while describing a different NUMBER of nodes now differ on a line instead of agreeing. It was
	// previously reported only to the caller's log, where no file comparison could ever see it.
	Lines.Add(FString::Printf(TEXT("GRAPH\tNodeCount\t%d"), NodeTags.Num()));

	for (const FName& Tag : NodeTags)
	{
		const UQuestNodeBase* Node = Graph.GetCompiledNodes()[Tag];
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
	OutNodeCount = NodeTags.Num();
	return Lines;
}

