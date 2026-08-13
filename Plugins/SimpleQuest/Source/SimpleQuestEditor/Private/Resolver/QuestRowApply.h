// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Execute a row plan against the Data Table it was computed for. The outbound mirror of QuestInPlaceApply, and it
// keeps that file's two contracts: refuse wholesale rather than apply the clean part, and let the CALLER own the
// transaction so an apply and everything around it form one undoable unit.

#include "CoreMinimal.h"
#include "Resolver/QuestInPlacePlan.h"

class UDataTable;
struct FQuestDataRow;

/**
 * Write the plan's rows into Destination: creates seeded from the struct's defaults, updates writing only the cells the
 * plan named. Rows the plan does not mention are never touched - the claim set decided that at planning time, and this
 * step does not revisit it.
 *
 * RowsByKey supplies the incoming row for each Create, which carries no changes of its own for the same reason an
 * inbound Create does: there is nothing to diff against yet, so the whole row is written.
 */
void ApplyQuestRowPlan(UDataTable& Destination, const FQuestInPlacePlan& Plan, const TMap<FString, const FQuestDataRow*>& RowsByKey, FQuestApplyResult& OutResult);

