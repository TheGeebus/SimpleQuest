// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestNodeIdentity.h"
#include "QuestGraphBuilder.h"
#include "SimpleQuestLog.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/QuestlineGraphSchema.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Resolver/QuestEdgeVerbs.h"
#include "Rewards/QuestRewardBase.h"
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
		// the source dropped - and under a delete-orphans policy, an apply would remove them and sever the wires through them.
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
			// A container's inner level is named by the container's own key - the same convention the row 'graph' cell uses.
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

void BuildQuestNodeKeyIndex(const TMap<FString, FString>& SourceKeyByGuid, const TArray<FString>& AllGuids,	TMap<FString, FString>& OutGuidByKey, TArray<FString>& OutAmbiguousKeys)
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
	return LevelName;   // names no node we know - the caller decides whether that is a create or a refusal
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
		// An FInstancedStruct IS a child: its contents are authored in place and carry their own type, exactly like a
		// subobject. Everything else is inspected for subobjects nested inside it (FQuestRewardSet holds an array of
		// rewards this way) - the struct itself stays a plain value.
		if (Struct->Struct == FInstancedStruct::StaticStruct())
		{
			return true;
		}
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

void ForEachQuestInstancedChild(
	const FProperty* Prop,
	const void* ValuePtr,
	const FString& OwnerKey,
	const FString& PathPrefix,
	TFunctionRef<void(const FString& ChildKey, const FString& Path, const FQuestInstancedChild& Child, int32 ArrayOrdinal)> Visit,
	int32 ArrayOrdinal)
{
	// An FInstancedStruct's contents: one child, typed by whatever it currently holds. An UNSET one yields nothing, for
	// the same reason a null object slot does - absence is the honest representation, and emitting a typeless row would
	// give import nothing to reconstruct from.
	if (const FStructProperty* AsStruct = CastField<FStructProperty>(Prop))
	{
		if (AsStruct->Struct == FInstancedStruct::StaticStruct())
		{
			const FInstancedStruct& Inst = *static_cast<const FInstancedStruct*>(ValuePtr);
			if (const UScriptStruct* Type = Inst.GetScriptStruct())
			{
				FQuestInstancedChild Child;
				Child.StructType = Type;
				Child.Memory = Inst.GetMemory();
				Visit(FString::Printf(TEXT("%s/%s"), *OwnerKey, *PathPrefix), PathPrefix, Child, ArrayOrdinal);
			}
			return;
		}
	}

	// Direct instanced object: one child. A null slot yields nothing — absence is the honest representation.
	if (const FObjectProperty* Obj = CastField<FObjectProperty>(Prop))
	{
		if (const UObject* Sub = Obj->GetObjectPropertyValue(ValuePtr))
		{
			FQuestInstancedChild Child;
			Child.Object = Sub;
			Visit(FString::Printf(TEXT("%s/%s"), *OwnerKey, *PathPrefix), PathPrefix, Child, ArrayOrdinal);
		}
		return;
	}
	// Array: each element carries a bracketed segment. WHERE THE ELEMENT IS THE CHILD, that segment is the child's own
	// IDENTITY rather than its position - a positional key is minted from the live array length, so two people each
	// appending one child both mint the same next index, and because sibling rows are filed by CLASS those land in
	// different files where a text merge sees no overlap at all. Where the element merely CONTAINS a child (an array
	// of structs) the index stays: that segment is structural path, and a struct element has no identity to use.
	if (const FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
	{
		const FObjectProperty* InnerObj = CastField<FObjectProperty>(Arr->Inner);
		const bool bElementIsChild = InnerObj && Arr->Inner->HasAnyPropertyFlags(CPF_InstancedReference);

		FScriptArrayHelper Helper(Arr, ValuePtr);
		for (int32 Idx = 0; Idx < Helper.Num(); ++Idx)
		{
			FString Segment = FString::Printf(TEXT("%s[%d]"), *PathPrefix, Idx);
			if (bElementIsChild)
			{
				// Rewards are the only instanced-child kind the corpus has. Naming the class here is honest about
				// that; a second kind turns this into a small dispatch rather than a rewrite.
				const UObject* Element = InnerObj->GetObjectPropertyValue(Helper.GetRawPtr(Idx));
				if (const UQuestRewardBase* Reward = Cast<UQuestRewardBase>(Element))
				{
					if (Reward->RewardGuid.IsValid())
					{
						Segment = FString::Printf(TEXT("%s[%s]"), *PathPrefix, *Reward->RewardGuid.ToString(EGuidFormats::Digits));
					}
				}
			}
			// The ordinal rides SEPARATELY: position is still meaning for a reward array (grant sequence), it just
			// stops being identity. INDEX_NONE where the element is not itself the child - it has no position of its own.
			ForEachQuestInstancedChild(Arr->Inner, Helper.GetRawPtr(Idx), OwnerKey, Segment, Visit,
			                           bElementIsChild ? Idx : INDEX_NONE);
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
	// dropped - the only corpus case is a reward set whose sole field IS the instanced array.
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
	const FString Verb = QuestEdgeVerbs::VerbForPinCategory(PinCategory);
	// The "wire:" spelling stays HERE rather than in the shared table: it is this path's way of writing down a category it
	// does not have a verb for, and the import direction has no matching parser for it. An asymmetry worth leaving visible
	// rather than burying in a table that would then look bidirectional.
	return Verb.IsEmpty() ? FString::Printf(TEXT("wire:%s"), *PinCategory.ToString()) : Verb;
}

void CollectQuestWireEdges(const UQuestlineNodeBase* Node, const FQuestlineGraphTraversalPolicy& Policy, TArray<FQuestDataEdge>& OutEdges)
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
				continue;   // non-questline node downstream - shouldn't occur; skip defensively
			}
			const FString Type = FString::Printf(TEXT("%s(%s)"), *QuestEdgeVerb(Pin->PinType.PinCategory), *Pin->PinName.ToString());
			OutEdges.Add({ FromKey, Type, ToNode->QuestGuid.ToString(EGuidFormats::Digits) });
		}
	}
}

void CompareQuestEdges(const TArray<FQuestDataEdge>& Incoming, const TArray<FQuestDataEdge>& Live, const TMap<FString, FString>& GuidByKey,
					   const TMap<FString, const UQuestlineNodeBase*>& NodeByGuid, TArray<FQuestDataEdge>& OutAdded, TArray<FQuestDataEdge>& OutRemoved)
{
	auto Canonical = [&GuidByKey, &NodeByGuid](const FQuestDataEdge& Edge)
	{
		const FString* From = GuidByKey.Find(Edge.From);
		const FString* To   = GuidByKey.Find(Edge.To);
		const FString FromGuid = From ? *From : Edge.From;

		// RESOLVE THE QUALIFIER THE WAY APPLY WILL. A qualifier is a SELECTOR, not a value: "outcome(Solved)" names a
		// pin by its authored label and "activates()" names a node's primary forward pin, while an edge read off the
		// graph carries the pin's real FName - which for an outcome is the full tag. Compared as text those never
		// agree, so a clean re-import reported every wire-bound edge as a removal AND an addition.
		// Going through ResolveQuestSourcePin is the point rather than an implementation detail: QuestInPlaceApply
		// calls that same function to execute both the added and the removed edges, so "the differ sees no change"
		// and "apply would wire the same pin" cannot come apart. Any normalization written fresh here could.
		// The verb is recomposed from the RESOLVED pin's category rather than reused from the incoming text, so both
		// sides land on the byte-identical string CollectQuestWireEdges builds.
		FString Type = Edge.Type;
		if (const UQuestlineNodeBase* const* Node = NodeByGuid.Find(FromGuid))
		{
			if (const UEdGraphPin* Pin = ResolveQuestSourcePin(const_cast<UQuestlineNodeBase*>(*Node), Edge.Type))
			{
				Type = FString::Printf(TEXT("%s(%s)"), *QuestEdgeVerb(Pin->PinType.PinCategory), *Pin->PinName.ToString());
				if (Type != Edge.Type)
				{
					UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("CompareQuestEdges: qualifier '%s' resolved to '%s' on node %s."),
						*Edge.Type,
						*Type,
						*FromGuid);
				}
			}
		}
		return FQuestDataEdge{ FromGuid, Type, To ? *To : Edge.To };
	};
	
	auto Id       = [](const FQuestDataEdge& E) { return FString::Printf(TEXT("%s|%s|%s"), *E.From, *E.Type, *E.To); };
	auto IsWiring = [](const FQuestDataEdge& E) { return !E.Type.StartsWith(TEXT("contains(")); };

	TMap<FString, FQuestDataEdge> LiveById;
	for (const FQuestDataEdge& Edge : Live)
	{
		if (IsWiring(Edge)) { const FQuestDataEdge C = Canonical(Edge); LiveById.Add(Id(C), C); }
	}
	TMap<FString, FQuestDataEdge> IncomingById;
	for (const FQuestDataEdge& Edge : Incoming)
	{
		if (IsWiring(Edge)) { const FQuestDataEdge C = Canonical(Edge); IncomingById.Add(Id(C), C); }
	}

	for (const TPair<FString, FQuestDataEdge>& Pair : IncomingById) { if (!LiveById.Contains(Pair.Key))     { OutAdded.Add(Pair.Value); } }
	for (const TPair<FString, FQuestDataEdge>& Pair : LiveById)     { if (!IncomingById.Contains(Pair.Key)) { OutRemoved.Add(Pair.Value); } }

	auto ByIdentity = [](const FQuestDataEdge& A, const FQuestDataEdge& B)
	{
		return FString::Printf(TEXT("%s|%s|%s"), *A.From, *A.Type, *A.To) < FString::Printf(TEXT("%s|%s|%s"), *B.From, *B.Type, *B.To);
	};
	OutAdded.Sort(ByIdentity);     // deterministic reporting; map iteration order is not
	OutRemoved.Sort(ByIdentity);
}

