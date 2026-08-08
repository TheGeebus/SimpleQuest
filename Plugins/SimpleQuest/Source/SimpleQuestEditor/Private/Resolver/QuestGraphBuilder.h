// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Rows into editor nodes, edges into links. The only file that constructs UEdGraphNodes from bundle data: spawn with the
// exported identity adopted rather than regenerated, refresh property-derived pins innermost-first so a container's pins
// exist before anything wires to them, resolve a "verb(pin)" edge to the two pins it names, and connect.

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
struct FQuestDataBundle;
struct FQuestDataRow;

/**
 * Spawn one editor node of the row's class into TargetGraph, adopt the exported identity, restore properties and
 * instanced children. GUID preservation is assignment order - Finalize regenerates, so we overwrite afterwards. Returns
 * the node and maps its exported key to the live node, so edge resolution and the pin pass can find it later.
 */
UEdGraphNode* SpawnQuestNodeFromRow(UEdGraph* TargetGraph, const FQuestDataRow& Row, const FQuestDataBundle& Bundle,
									TMap<FString, UEdGraphNode*>& NodeByKey, TSet<FString>& Consumed, TArray<FString>& Warnings);

/**
 * Populate one graph level from every row whose "graph" cell names it, recursing into container inner graphs. Clears the
 * level's default nodes first, so the level is built entirely from rows including the exported Entry - otherwise the
 * schema's own Entry and the imported one would both exist.
 */
void ImportQuestGraphLevel(UEdGraph* TargetGraph, const FString& GraphCell, const FQuestDataBundle& Bundle,
						   const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, TMap<FString, UEdGraphNode*>& NodeByKey,
						   TSet<FString>& Consumed, TArray<FString>& Warnings);

/**
 * Rebuild the pins whose existence depends on restored property values (outcome pins, variadic condition pins). Runs
 * DEEPEST-FIRST, because a node inside a container needs the container's own pins to already exist.
 */
void RefreshQuestNodePins(const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey,
						  const TMap<FString, UEdGraphNode*>& NodeByKey, TArray<FString>& Warnings);

/** Resolve an edge's "verb(qualifier)" form to the output pin on Node that it names. Null when no pin matches. */
UEdGraphPin* ResolveQuestSourcePin(UEdGraphNode* Node, const FString& EdgeType);

/** Resolve the input pin on Node that accepts a connection from a source pin of SourceCategory. Null when none fits. */
UEdGraphPin* ResolveQuestDestPin(UEdGraphNode* Node, FName SourceCategory);

/** Connect every edge in the bundle, resolving both endpoints by key and both pins by category. Warns and skips on a miss. */
void WireQuestEdges(const FQuestDataBundle& Bundle, const TMap<FString, UEdGraphNode*>& NodeByKey,
					const TSet<FString>& ConsumedChildKeys, TArray<FString>& Warnings);