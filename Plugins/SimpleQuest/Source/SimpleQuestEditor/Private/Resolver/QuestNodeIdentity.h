// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The one walk that reads a questline graph's node IDENTITY, recursing through container inner graphs. Both directions of
// the data pipeline need it: export writes each node's key into its rows, and in-place re-import matches incoming rows back
// onto the nodes that already exist. Keeping a single walker means the two directions can never disagree about which node a
// key refers to.

#include "CoreMinimal.h"

class UEdGraph;
class UQuestlineNodeBase;

/**
 * Collect node identity from a graph and every container inner graph beneath it.
 * @param EdGraph              Graph to walk; null is a no-op.
 * @param OutSourceKeyByGuid   GUID -> the studio-authored key the node was imported under. Only nodes that carry one appear.
 * @param OutNodeByGuid        GUID -> node.
 * @param OutGraphCellByGuid   Optional. GUID -> the graph level the node sits in: "root", else the owning container's key.
 * @param GraphCell            The level being walked. Callers use the default; recursion supplies the container's key.
 */
void CollectQuestNodeIdentity(const UEdGraph* EdGraph,
	TMap<FString, FString>& OutSourceKeyByGuid,
	TMap<FString, const UQuestlineNodeBase*>& OutNodeByGuid,
	TMap<FString, FString>* OutGraphCellByGuid = nullptr,
	const FString& GraphCell = TEXT("root"));

/** The key a node is addressed by: its studio-authored key when it has one, else its GUID. Mirrors how rows are keyed. */
FString QuestNodeIdentityKey(const FString& Guid, const TMap<FString, FString>& SourceKeyByGuid);