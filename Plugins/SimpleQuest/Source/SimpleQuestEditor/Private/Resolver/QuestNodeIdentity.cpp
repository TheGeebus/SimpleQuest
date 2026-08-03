// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestNodeIdentity.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/QuestlineGraphSchema.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "UObject/UnrealType.h"


void CollectQuestNodeIdentity(const UEdGraph* EdGraph, TMap<FString, FString>& OutSourceKeyByGuid, TMap<FString, const UQuestlineNodeBase*>& OutNodeByGuid, TMap<FString, FString>* OutGraphCellByGuid, const FString& GraphCell)
{
	if (!EdGraph) return;
	for (const UEdGraphNode* RawNode : EdGraph->Nodes)
	{
		const UQuestlineNodeBase* Node = Cast<UQuestlineNodeBase>(RawNode);
		if (!Node) continue;

		// Reroute knots are wire furniture, not entities: the writer collapses them into the wire walk and emits no row for
		// one, so no source can ever mention a knot. Collecting them here would make every knot in the asset look like a row
		// the source dropped — and under a delete-orphans policy, an apply would remove them and sever the wires through them.
		if (Node->IsPassThroughNode()) continue;

		const FString Guid = Node->QuestGuid.ToString(EGuidFormats::Digits);
		OutNodeByGuid.Add(Guid, Node);
		if (!Node->ImportSourceKey.IsEmpty())
		{
			OutSourceKeyByGuid.Add(Guid, Node->ImportSourceKey);
		}
		if (OutGraphCellByGuid)
		{
			OutGraphCellByGuid->Add(Guid, GraphCell);
		}

		if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
		{
			// A container's inner level is named by the container's own key — the same convention the row 'graph' cell uses.
			const FString InnerCell = Node->ImportSourceKey.IsEmpty() ? Guid : Node->ImportSourceKey;
			CollectQuestNodeIdentity(QuestNode->GetInnerGraph(), OutSourceKeyByGuid, OutNodeByGuid, OutGraphCellByGuid, InnerCell);
		}
	}
}

FString QuestNodeIdentityKey(const FString& Guid, const TMap<FString, FString>& SourceKeyByGuid)
{
	const FString* Semantic = SourceKeyByGuid.Find(Guid);
	return Semantic ? *Semantic : Guid;
}

void BuildQuestNodeKeyIndex(const TMap<FString, FString>& SourceKeyByGuid, const TArray<FString>& AllGuids,
							TMap<FString, FString>& OutGuidByKey, TArray<FString>& OutAmbiguousKeys)
{
	// Collect claims first, resolve after: a key is only trustworthy once every claimant is known, so a single pass that
	// wrote as it went would have to decide a collision before it could see there was one.
	TMap<FString, TArray<FString>> GuidsByName;
	for (const FString& Guid : AllGuids)
	{
		GuidsByName.FindOrAdd(Guid).AddUnique(Guid);                       // every node answers to its own GUID
		if (const FString* Semantic = SourceKeyByGuid.Find(Guid))
		{
			GuidsByName.FindOrAdd(*Semantic).AddUnique(Guid);              // ...and to the key it was imported under
		}
	}

	// One name naming two nodes: neither can be addressed by it. Report the name and drop it entirely rather than letting
	// hash order decide which node a row edits and which one it deletes.
	TSet<FString> ClaimedGuids;
	for (const TPair<FString, TArray<FString>>& Pair : GuidsByName)
	{
		if (Pair.Value.Num() != 1) { OutAmbiguousKeys.AddUnique(Pair.Key); continue; }
		OutGuidByKey.Add(Pair.Key, Pair.Value[0]);
		ClaimedGuids.Add(Pair.Value[0]);
	}
	OutAmbiguousKeys.Sort();   // deterministic reporting; the map's iteration order is not
}

FString ResolveQuestLevelToGuid(const FString& LevelName, const TMap<FString, FString>& GuidByKey)
{
	if (LevelName.IsEmpty() || LevelName == TEXT("root")) { return LevelName; }
	if (const FString* Guid = GuidByKey.Find(LevelName)) { return *Guid; }
	return LevelName;   // names no node we know — the caller decides whether that is a create or a refusal
}

namespace
{
	// Child keys travel in tab-separated tables, so a map key's exported text must not carry framing characters.
	FString SanitizeChildKeySegment(const FString& In)
	{
		return In.Replace(TEXT("\t"), TEXT("\\t")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("\\n"));
	}
}

bool IsQuestInstancedBearing(const FProperty* Prop)
{
	if (const FObjectProperty* Obj = CastField<FObjectProperty>(Prop))
	{
		return Obj->HasAnyPropertyFlags(CPF_InstancedReference);
	}
	if (const FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
	{
		return IsQuestInstancedBearing(Arr->Inner);
	}
	if (const FMapProperty* Map = CastField<FMapProperty>(Prop))
	{
		return IsQuestInstancedBearing(Map->ValueProp);
	}
	if (const FStructProperty* Struct = CastField<FStructProperty>(Prop))
	{
		for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
		{
			if (IsQuestInstancedBearing(*It))
			{
				return true;
			}
		}
	}
	return false;
}

void ForEachQuestInstancedChild(const FProperty* Prop, const void* ValuePtr, const FString& OwnerKey, const FString& PathPrefix,
                                TFunctionRef<void(const FString& ChildKey, const FString& Path, const UObject* Child)> Visit)
{
	// Direct instanced object: one child. A null slot yields nothing — absence is the honest representation.
	if (const FObjectProperty* Obj = CastField<FObjectProperty>(Prop))
	{
		if (const UObject* Sub = Obj->GetObjectPropertyValue(ValuePtr))
		{
			Visit(FString::Printf(TEXT("%s/%s"), *OwnerKey, *PathPrefix), PathPrefix, Sub);
		}
		return;
	}
	// Array: each element carries an [i] path segment.
	if (const FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper Helper(Arr, ValuePtr);
		for (int32 Idx = 0; Idx < Helper.Num(); ++Idx)
		{
			ForEachQuestInstancedChild(Arr->Inner, Helper.GetRawPtr(Idx), OwnerKey,
				FString::Printf(TEXT("%s[%d]"), *PathPrefix, Idx), Visit);
		}
		return;
	}
	// Map: each VALUE carries a [KeyExport] segment (corpus case: questline rewards keyed by outcome tag).
	if (const FMapProperty* Map = CastField<FMapProperty>(Prop))
	{
		FScriptMapHelper Helper(Map, ValuePtr);
		for (FScriptMapHelper::FIterator It(Helper); It; ++It)
		{
			FString KeyExport;
			Map->KeyProp->ExportTextItem_Direct(KeyExport, Helper.GetKeyPtr(It), nullptr, nullptr, PPF_None);
			ForEachQuestInstancedChild(Map->ValueProp, Helper.GetValuePtr(It), OwnerKey,
				FString::Printf(TEXT("%s[%s]"), *PathPrefix, *SanitizeChildKeySegment(KeyExport)), Visit);
		}
		return;
	}
	// Struct: descend its instanced-bearing fields, extending the path with the field name. Non-instanced siblings are
	// dropped — the only corpus case is a reward set whose sole field IS the instanced array.
	if (const FStructProperty* Struct = CastField<FStructProperty>(Prop))
	{
		for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
		{
			if (!IsQuestInstancedBearing(*It))
			{
				continue;
			}
			ForEachQuestInstancedChild(*It, It->ContainerPtrToValuePtr<void>(ValuePtr), OwnerKey,
				FString::Printf(TEXT("%s.%s"), *PathPrefix, *It->GetName()), Visit);
		}
	}
}

FString QuestEdgeVerb(FName PinCategory)
{
	if (PinCategory == TEXT("QuestActivation"))   return TEXT("activates");
	if (PinCategory == TEXT("QuestOutcome"))      return TEXT("outcome");
	if (PinCategory == TEXT("QuestPrerequisite")) return TEXT("feeds-prereq");
	// Output-side category is past-tense "QuestDeactivated" (the input side's "QuestDeactivate" never appears as an edge
	// source — sources are always output pins).
	if (PinCategory == TEXT("QuestDeactivated"))  return TEXT("deactivates");
	return FString::Printf(TEXT("wire:%s"), *PinCategory.ToString());
}

void CollectQuestWireEdges(const UQuestlineNodeBase* Node, const FQuestlineGraphTraversalPolicy& Policy,
						   TArray<FQuestDataEdge>& OutEdges)
{
	if (!Node) return;
	const FString FromKey = Node->QuestGuid.ToString(EGuidFormats::Digits);
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Output)
		{
			continue;
		}
		// Fresh Visited per source pin: the walker's visited set is node-granular, so sharing one across pins would
		// suppress legitimate edges from later pins.
		TArray<const UEdGraphPin*> Terminals;
		TSet<const UEdGraphNode*> Visited;
		Policy.CollectDownstreamTerminalInputs(Pin, Terminals, Visited);
		for (const UEdGraphPin* Terminal : Terminals)
		{
			const UQuestlineNodeBase* ToNode = Cast<UQuestlineNodeBase>(Terminal->GetOwningNode());
			if (!ToNode)
			{
				continue;   // non-questline node downstream — shouldn't occur; skip defensively
			}
			const FString Type = FString::Printf(TEXT("%s(%s)"), *QuestEdgeVerb(Pin->PinType.PinCategory), *Pin->PinName.ToString());
			OutEdges.Add({ FromKey, Type, ToNode->QuestGuid.ToString(EGuidFormats::Digits) });
		}
	}
}

