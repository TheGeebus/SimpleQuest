// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The read-only half of in-place re-import. Compares a validated bundle against an existing questline and produces the
// plan as DATA: it mutates nothing, logs nothing, and renders nothing, so the same plan can be shown in a panel, written
// to a file by a headless commandlet, or printed by a console command without any of them being the privileged caller.

#include "CoreMinimal.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestInPlacePlan.h"

class UQuestlineGraph;
struct FQuestDataBundle;
struct FQuestDataRow;

/**
 * Compare a validated bundle against an EXISTING asset and describe what a re-import would do, changing nothing.
 * ReadWarnings are carried into the plan rather than re-derived, so everything a caller needs to report the run arrives
 * in one structure. Policies decides what an ABSENT cell means, and is resolved by the caller so the plan is only ever
 * interpreted against the policy that produced it.
 */
void PlanQuestInPlace(const UQuestlineGraph& Target, const FQuestDataBundle& Bundle,
					  const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, const TArray<FString>& ReadWarnings,
					  FQuestInPlacePlan& OutPlan,
					  const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver());

