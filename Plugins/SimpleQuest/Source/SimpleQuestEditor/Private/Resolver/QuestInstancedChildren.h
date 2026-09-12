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
 * PROPERTY-DRIVEN (D2): walk the layout's instanced-bearing properties, rebuild each child from its child row (matched
 * by key), in array/map order. The child KEY carries the position (the layout already knows the property + container
 * type from reflection), so the only parse is extracting the trailing [index] / [mapkey] segment. Records every
 * child key it consumed into OutConsumed so P-final can cross-check against the contains edges (D1's completeness
 * property, kept as a tripwire rather than the reconstruction path).
 *
 * The general form walks any reflected container: a UObject's class over itself, or a script struct over its memory -
 * which is how one outcome's reward set, a struct value inside the questline's map, rebuilds its inline rewards through
 * the same array walk a node does rather than a copy of it. Outer is the object new subobjects are created under, and
 * is the owning UObject whichever container is being walked.
 */
void ReattachQuestInstancedChildren(const UStruct* Layout, void* Container, UObject* Outer, const FString& OwnerKey,
	const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings);

/** The UObject case - every caller today. Forwards to the general form with the object as both container and outer. */
void ReattachQuestInstancedChildren(UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings);

