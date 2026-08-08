// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The bundle-to-bundle transforms the resolver pipeline is built from, given external linkage so they can be exercised
// directly. Their definitions stay with the import/export routing they belong to; these are thin, stable entry points so a
// caller does not have to reach into a translation unit's internals. Every one is pure data in, pure data out — no world,
// no asset, no files — which is precisely what makes the forward/reverse round-trip property testable.

#include "CoreMinimal.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestInPlacePlan.h"


struct FQuestApplyResult;
struct FQuestNodePlanEntry;
struct FQuestDataRow;
struct FQuestDataBundle;
struct FQuestDataValue;
struct FQuestInPlacePlan;
struct FQuestPropertyChange;
class FProperty;
class UQuestImportMapping;
class UQuestlineGraph;
class UQuestlineNodeBase;

/** Studio-shaped bundle -> canonical: route rows to classes by the discriminator, rename bound columns. False = refused. */
bool QuestBundle_ApplyMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings);

/** Synthesize edges from row-adjacent wire columns, then strip those cells. */
void QuestBundle_ApplyWireBindings(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings);

/** Canonical bundle -> studio-shaped: the same recipe read backwards. Pass empty maps to exercise it without a graph. */
void QuestBundle_ApplyReverseMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, const TMap<FString, FString>& SourceKeyByGuid, const TMap<FString, const UQuestlineNodeBase*>& NodeByGuid, TArray<FString>& Warnings);

/** Compare a bundle against an existing questline asset and describe what a re-import would change. Mutates nothing. */
void QuestBundle_PlanInPlace(const UQuestlineGraph& Target, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, const TArray<FString>& ReadWarnings, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver());

/**
 * Structural check on a read bundle: every row keyed, every class resolvable, every graph cell reachable, no duplicate
 * or contested keys. Refuses on any inconsistency rather than letting a partial asset be built from a malformed source.
 * Provider-agnostic - a bad bundle from any format is refused identically.
 * NodeRowsByKey comes back holding pointers INTO Bundle, so the two have the same lifetime; AllRowKeys additionally
 * includes instanced-child keys, which NodeRowsByKey deliberately excludes.
 */
bool QuestBundle_Validate(const FQuestDataBundle& Bundle, TMap<FString, const FQuestDataRow*>& NodeRowsByKey, TSet<FString>& AllRowKeys, FString& OutError);

