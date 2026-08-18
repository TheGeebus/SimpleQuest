// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The reflection-driven walk that turns a live questline graph into a bundle: one row per node and per instanced
// sub-object, one knot-collapsed edge table, container inner graphs recursed, LinkedQuestline placements deliberately
// not. The mirror of QuestGraphBuilder, and separate from the transforms because neither direction knows anything about
// a studio recipe - a bundle produced here is already in our own shape.

#include "CoreMinimal.h"

class UEdGraph;
class UObject;
struct FQuestDataBundle;
class FQuestlineGraphTraversalPolicy;

/**
 * Emit one entity's row into the bundle - its authored config cells, plus any ExtraCells the caller stamps on - and
 * recurse its instanced sub-objects into rows of their own. Used for both a node and the questline asset itself, so the
 * row shape has one definition rather than one per caller.
 */
void CollectQuestEntityRow(const UObject* Entity, const FString& Key, const TMap<FString, FString>& ExtraCells,
						   FQuestDataBundle& Bundle);

/**
 * Walk one graph level into the bundle: a row and its wire edges per node, then recursion into any container's inner
 * graph under the container's own key. GraphCell names the level a row belongs to, "root" at the top.
 */
void CollectQuestGraphBundle(const UEdGraph* Graph, const FString& GraphCell,
							 const FQuestlineGraphTraversalPolicy& Policy, FQuestDataBundle& Bundle);

