// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The row-to-object writer: which class a row's "class" cell names, one cell onto one property, and a whole row onto a
// whole object. Everything that puts source data into a live UObject funnels through here, which is what makes "did the
// write actually happen?" a single answerable question rather than one each caller answers for itself.

#include "CoreMinimal.h"

struct FQuestDataRow;
struct FQuestDataValue;
class FProperty;

/**
 * Resolve a class named by a bundle cell. Bundle files carry SHORT names deliberately - they are what a studio reads and
 * diffs - so this tries the qualified form against our own script packages before falling back to the slow global search.
 */
UClass* ResolveQuestBundleClass(const FString& ClassName);

/**
 * Write one cell onto one property, returning whether it ACTUALLY WROTE. The return is the contract: a caller that
 * dirties a package or counts a change must gate on it, because several arms deliberately decline - an Empty cell, a
 * type mismatch, an array whose elements did not all land - and leave the destination at its existing value.
 */
bool RestoreQuestCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value);

/**
 * Apply every cell in Row to Target's matching UPROPERTY by column name. Skips structural columns and any column with no
 * matching property, so a stale table column cannot abort an import.
 */
void RestoreQuestRowProperties(UObject* Target, const FQuestDataRow& Row);

/**
 * Re-express an incoming cell in the SAME typed form a live property produces, so the two can be compared meaningfully.
 * Exposed to pin the seed rule: a non-writing arm must leave the seed in place, which is what makes an absent cell mean
 * "unchanged" rather than "reset". Tests are its only caller - the diff path currently inlines an equivalent sequence.
 */
FQuestDataValue TypeQuestCellLikeProperty(const FProperty* Prop, const FQuestDataValue& Cell, const void* SeedPtr);

