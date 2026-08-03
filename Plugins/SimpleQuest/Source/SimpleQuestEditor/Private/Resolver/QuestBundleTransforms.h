// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The bundle-to-bundle transforms the resolver pipeline is built from, given external linkage so they can be exercised
// directly. Their definitions stay with the import/export routing they belong to; these are thin, stable entry points so a
// caller does not have to reach into a translation unit's internals. Every one is pure data in, pure data out — no world,
// no asset, no files — which is precisely what makes the forward/reverse round-trip property testable.

#include "CoreMinimal.h"

struct FQuestDataRow;
struct FQuestDataBundle;
struct FQuestDataValue;
struct FQuestInPlacePlan;
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

/** Write one bundle cell into a typed property value. Exposed so the Kind-to-property pairing's type safety is testable. */
void QuestBundle_RestoreCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value);

/** Rebuild an owner's instanced children from the bundle's child rows. Exposed so the declare-versus-silence rule is testable. */
void QuestBundle_ReattachInstanced(UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings);

/** Synthesize prerequisite combinators from row-adjacent convention columns, then strip those cells. */
void QuestBundle_ApplyFlowConventions(FQuestDataBundle& Bundle, TArray<FString>& Warnings);

/** Re-express a bundle cell in the typed form a live property produces, starting from SeedPtr. Exposed to pin the seed rule. */
FQuestDataValue QuestBundle_TypeIncomingLikeProperty(const FProperty* Prop, const FQuestDataValue& Cell, const void* SeedPtr);

/** Compare a bundle against an existing questline asset and describe what a re-import would change. Mutates nothing. */
void QuestBundle_PlanInPlace(const UQuestlineGraph& Target, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, const TArray<FString>& ReadWarnings, FQuestInPlacePlan& OutPlan);

