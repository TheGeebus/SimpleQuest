// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The one walk that reads a questline graph's node IDENTITY, recursing through container inner graphs. Both directions of
// the data pipeline need it: export writes each node's key into its rows, and in-place re-import matches incoming rows back
// onto the nodes that already exist. Keeping a single walker means the two directions can never disagree about which node a
// key refers to.

#include "CoreMinimal.h"
#include "Resolver/QuestDataBundle.h"
#include "Templates/Function.h"

class FProperty;
class FQuestlineGraphTraversalPolicy;
class UObject;
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
void CollectQuestNodeIdentity(
	const UEdGraph* EdGraph,
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
void BuildQuestNodeKeyIndex(
	const TMap<FString, FString>& SourceKeyByGuid,
	const TArray<FString>& AllGuids,
	TMap<FString, FString>& OutGuidByKey,
	TArray<FString>& OutAmbiguousKeys);

/** Resolve a graph-level name to the one namespace: "root" stays "root", any other spelling resolves to the owning node's GUID. */
FString ResolveQuestLevelToGuid(const FString& LevelName, const TMap<FString, FString>& GuidByKey);

/**
 * True when a property's value(s) are Instanced UObjects — the shapes that must explode to child rows rather than
 * serialize as a dangling object path. Recurses array inners, map values and struct fields, so container-wrapped instanced
 * data (e.g. TMap<FGameplayTag, FQuestRewardSet> wrapping an instanced array) classifies correctly.
 */
bool IsQuestInstancedBearing(const FProperty* Prop);

/**
 * Visit every instanced child reachable from Prop, deriving the SAME key the writer emits for it. ONE level: a child's own
 * instanced properties are the caller's to descend, which is what lets the export and the planner do different things at
 * each level while agreeing exactly on names.
 * Both directions walk this — the export to emit a row per child, the planner to find the live child a row refers to.
 * A key derived two ways is a key that eventually disagrees, and a child addressed by a name nobody else uses is a child
 * that silently disappears.
 * @param PathPrefix  the property path so far, relative to OwnerKey (e.g. "Rewards" or "QuestlineRewards[<key>].Rewards").
 */
void ForEachQuestInstancedChild(
	const FProperty* Prop,
    const void* ValuePtr,
    const FString& OwnerKey,
    const FString& PathPrefix,
    TFunctionRef<void(const FString& ChildKey, const FString& Path, const UObject* Child, int32 ArrayOrdinal)> Visit,
    int32 ArrayOrdinal = INDEX_NONE);

/** Wire-edge verb for a source pin category. Every edge is written output->input, and the verbs read true in that direction. */
FString QuestEdgeVerb(FName PinCategory);

/**
 * Knot-collapsed wire edges leaving one node, appended to OutEdges. Every output pin's terminals via the traversal policy's
 * forward walk, so a chain of reroutes reads as the single edge it means (the zero-knot case degenerates to the direct link).
 * Uses a FRESH visited set per source pin: the walker's set is node-granular, so sharing one across pins would suppress
 * legitimate edges from later pins.
 * Shared for the same reason the child walk is: the reader that wants to know how an asset is CURRENTLY wired must derive
 * edges exactly as the writer does, or the two disagree about wiring that never changed.
 */
void CollectQuestWireEdges(const UQuestlineNodeBase* Node, const FQuestlineGraphTraversalPolicy& Policy, TArray<FQuestDataEdge>& OutEdges);

/**
 * Which wire edges would a re-import add, and which would it remove? Both sides are canonicalized through the key index
 * first: live edges are always GUID-keyed, while a source's edges carry whatever keys that source uses, so comparing them
 * raw reports every unchanged edge as both an addition AND a removal.
 * 'contains' edges are ignored — an inner-graph membership is covered by the graph-level comparison, and an instanced
 * child by the child diff, so including them here would double-report.
 * An endpoint naming no known node passes through unresolved, which correctly makes an edge touching a to-be-created node
 * an addition rather than a match.
 */
void CompareQuestEdges(
	const TArray<FQuestDataEdge>& Incoming,
    const TArray<FQuestDataEdge>& Live,
    const TMap<FString, FString>& GuidByKey,
    const TMap<FString, const UQuestlineNodeBase*>& NodeByGuid,
    TArray<FQuestDataEdge>& OutAdded,
    TArray<FQuestDataEdge>& OutRemoved);

