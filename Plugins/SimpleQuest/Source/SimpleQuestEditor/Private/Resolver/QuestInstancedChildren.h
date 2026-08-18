// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The bundle's instanced-child rows: find a child row by key, and rebuild an owner's instanced contents from the rows
// describing them. Owns the declare-versus-silence contract that both the planner and the apply path depend on - a source
// that DESCRIBES a property's children replaces them wholesale, and a source that says nothing leaves them alone, because
// silence is not an assertion of emptiness.

#include "CoreMinimal.h"

struct FQuestDataBundle;
struct FQuestDataRow;

/**
 * Find the row describing one instanced child, by its full key, and report the class its "class" cell names. Returns null
 * when no table holds that key - which the caller treats as a source that never described the child, not as an error.
 */
const FQuestDataRow* FindQuestChildRow(const FQuestDataBundle& Bundle, const FString& ChildKey, FString& OutClass);

/**
 * The one reattach primitive, used by both the self row (QuestlineRewards) and per-node rows (Rewards).
 * PROPERTY-DRIVEN (D2): walk Owner's instanced-bearing properties, rebuild each child from its child row (matched
 * by key), in array/map order. The child KEY carries the position (Owner already knows the property + container
 * type from reflection), so the only parse is extracting the trailing [index] / [mapkey] segment. Records every
 * child key it consumed into OutConsumed so P-final can cross-check against the contains edges (D1's completeness
 * property, kept as a tripwire rather than the reconstruction path).
 */
void ReattachQuestInstancedChildren(UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings);

