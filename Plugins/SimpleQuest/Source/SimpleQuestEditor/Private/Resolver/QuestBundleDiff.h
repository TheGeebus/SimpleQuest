// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Compare a live object against the rows describing it and say what a re-import WOULD change, changing nothing. Cell
// comparison and instanced-child topology comparison sit together because the second calls the first on every child it
// walks, and splitting them puts that call across a file boundary the dependency graph cannot afford.

#include "CoreMinimal.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestInPlacePlan.h"

struct FQuestDataBundle;
struct FQuestDataRow;
class UStruct;

/**
 * Compare one reflected container's properties against one row's cells - the general form, over any UStruct layout. A
 * UObject passes its UClass and itself; a Data Table row passes its row struct and the row's memory. ONE definition
 * matters more here than anywhere else in the resolver: it is what makes "a change" mean the same thing reading a table
 * into a graph and writing a graph back out into one, instead of two rules free to drift apart.
 * PathPrefix names where this container sits under its owner, empty at the top.
 */
void DiffQuestContainerAgainstRow(const UStruct* Layout, const void* Container, const FQuestDataRow& Row,
								  const FString& PathPrefix, FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan,
								  const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver());

/**
 * The UObject case - every caller today, and the one worth keeping legible: a node, the questline itself, an instanced
 * child. Forwards to the general form so no existing call site changes and neither does the rule it runs.
 */
void DiffQuestObjectAgainstRow(const UObject* Object, const FQuestDataRow& Row, const FString& PathPrefix,
							   FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan,
							   const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver());

/**
 * Compare an owner's live instanced children against the rows describing them, recording adds, removes and reorders.
 * Honors the declare-versus-silence contract: a property the bundle says nothing about is skipped entirely, because a
 * source that omits a property is not asserting that it should be emptied.
 */
void DiffQuestInstancedChildren(const UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle,
								FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan,
								const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver());

