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

/**
 * Index every node under BOTH names a source can legitimately address it by: its GUID and, when it has one, its
 * studio-authored key. Which name a source uses depends on how that source was produced, not on the node — a canonical
 * export writes GUID keys for every node, while a studio's own file writes semantic ones.
 * A key claimed by more than one node, or a node claimed by more than one key, is AMBIGUOUS: it is reported and left out
 * of the index entirely, so a caller refuses rather than picking a winner by hash order.
 */
void BuildQuestNodeKeyIndex(const TMap<FString, FString>& SourceKeyByGuid,
	const TArray<FString>& AllGuids,
	TMap<FString, FString>& OutGuidByKey,
	TArray<FString>& OutAmbiguousKeys);

/** Resolve a graph-level name to the one namespace: "root" stays "root", any other spelling resolves to the owning node's GUID. */
FString ResolveQuestLevelToGuid(const FString& LevelName, const TMap<FString, FString>& GuidByKey);
