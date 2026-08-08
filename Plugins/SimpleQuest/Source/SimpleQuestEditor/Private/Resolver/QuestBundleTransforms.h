// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The recipe applied to a bundle, both directions. QuestBundle_ApplyMapping routes a flat studio table's rows to classes by the
// discriminator and renames bound columns; ApplyWireBindings turns row-adjacent target columns into edges;
// QuestBundle_ApplyReverseMapping reads the identical recipe backwards to restate a canonical bundle in the studio's vocabulary; and
// Validate refuses anything structurally incoherent before a partial asset can exist. Forward and reverse live together
// because they are ONE RECIPE READ TWO WAYS, and drift between them is the failure this file exists to prevent. Every one
// is pure data in, pure data out — no world, no asset, no files — which is what makes the round-trip property testable.

#include "CoreMinimal.h"
#include "Resolver/QuestImportMapping.h"

struct FQuestDataRow;
struct FQuestDataBundle;
class UQuestImportMapping;
class UQuestlineNodeBase;

/** Studio-shaped bundle -> canonical: route rows to classes by the discriminator, rename bound columns. False = refused. */
bool QuestBundle_ApplyMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings);

/** Synthesize edges from row-adjacent wire columns, then strip those cells. */
void QuestBundle_ApplyWireBindings(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings);

/** Canonical bundle -> studio-shaped: the same recipe read backwards. Pass empty maps to exercise it without a graph. */
void QuestBundle_ApplyReverseMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, const TMap<FString, FString>& SourceKeyByGuid, const TMap<FString, const UQuestlineNodeBase*>& NodeByGuid, TArray<FString>& Warnings);

/**
 * Structural check on a read bundle: every row keyed, every class resolvable, every graph cell reachable, no duplicate
 * or contested keys. Refuses on any inconsistency rather than letting a partial asset be built from a malformed source.
 * Provider-agnostic - a bad bundle from any format is refused identically.
 * NodeRowsByKey comes back holding pointers INTO Bundle, so the two have the same lifetime; AllRowKeys additionally
 * includes instanced-child keys, which NodeRowsByKey deliberately excludes.
 */
bool QuestBundle_Validate(const FQuestDataBundle& Bundle, TMap<FString, const FQuestDataRow*>& NodeRowsByKey, TSet<FString>& AllRowKeys, FString& OutError);

