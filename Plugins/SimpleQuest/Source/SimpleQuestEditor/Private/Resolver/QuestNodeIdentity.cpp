// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestNodeIdentity.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"

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
