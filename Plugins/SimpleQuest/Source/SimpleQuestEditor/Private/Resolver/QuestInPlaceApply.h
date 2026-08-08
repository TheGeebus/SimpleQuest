// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Execute a plan against the asset it was computed for: create, move, refresh, wire, delete, write - in that order,
// because each pass depends on the previous one having finished. Refuses wholesale on any refusal or contested key
// rather than applying the clean part, so a rejected import leaves the asset exactly as it was.

#include "CoreMinimal.h"
#include "Resolver/QuestInPlacePlan.h"

class UQuestlineGraph;
struct FQuestDataBundle;
struct FQuestDataRow;

/**
 * Write one entity's property changes, returning how many actually landed. Resolves each change's path against the
 * objects the plan walked, and counts only writes that RestoreQuestCell confirms - a declined write is named in
 * OutSkipped rather than counted, so the caller never reports work it did not do.
 */
int32 ApplyQuestChangesToObject(UObject* Owner, const FString& OwnerKey, const TArray<FQuestPropertyChange>& Changes,
								TArray<FString>& OutSkipped);

/**
 * Execute the whole plan against Target. THE CALLER OWNS THE TRANSACTION - this deliberately opens none, so a caller
 * can wrap an apply and everything around it in one undoable unit. Issues a single graph notify at the end rather than
 * one per pass, and skips even that when nothing was written, so a no-op apply stays genuinely inert.
 */
void ApplyQuestPlan(UQuestlineGraph& Target, const FQuestInPlacePlan& Plan, const FQuestDataBundle& Bundle,
					const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, FQuestApplyResult& OutResult,
					const FQuestApplyOptions& Options = FQuestApplyOptions());