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

/**
 * Compare one object's authored properties against one row's cells. Shared by every object a bundle describes - a node,
 * the questline itself, and an instanced child - so the comparison rule has exactly one definition and cannot drift
 * between them. PathPrefix names where this object sits under its owner, empty at the top.
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

