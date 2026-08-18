// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// What writing this questline into a studio's Data Table WOULD change, computed without touching it. The outbound
// mirror of QuestInPlacePlanner, and deliberately the same plan type: a plan describes changes to entities, and
// direction is what says whether an entity is a node or a row.

#include "CoreMinimal.h"

class UDataTable;
class UQuestImportMapping;
struct FQuestDataBundle;
struct FQuestInPlacePlan;

/**
 * Compare a reverse-mapped bundle against the destination table and record what an apply would do: rows created, rows
 * updated, and rows left alone because this graph has no claim on them.
 *
 * TERRITORY IS NOT A PARAMETER because it is not a policy. Reverse mapping keys every row by QuestNodeIdentityKey - a
 * node's studio key when it has one, else its GUID - so the incoming key set IS the set of rows this graph claims.
 * Rows outside it are untouched and never proposed for removal: deleting a node destroys its claim, so removal is not
 * expressible from this side at all.
 *
 * Bundle must already have been through QuestBundle_ApplyReverseMapping - the flat "content" table is what a row write
 * consumes, and its absence is refused rather than assumed.
 */
void PlanQuestRowsIntoTable(const FQuestDataBundle& Bundle, const UDataTable& Destination, const UQuestImportMapping& Mapping, FQuestInPlacePlan& OutPlan);

