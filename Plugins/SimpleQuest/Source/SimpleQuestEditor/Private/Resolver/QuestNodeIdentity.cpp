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