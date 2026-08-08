// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Write-in authoring sugar for prerequisites. A flat source has no notion of edges or flow, so it declares a prereq as a
// COLUMN of operand row-keys (unlock_after:(step_a,step_b)) rather than hand-wiring a combinator, and this synthesizes the
// exact structural form the graph would have carried. Its own file rather than part of the mapping transforms because it is
// NOT recipe-driven: it runs unconditionally, on every import, whether or not a mapping is in play. The vocabulary is a
// table, so extending it is data rather than new control flow.

#include "CoreMinimal.h"

struct FQuestDataBundle;
struct FQuestDataValue;

/**
 * Read an operand-key list out of a convention cell. A structured provider (JSON) delivers Kind::Array and the elements
 * are used directly; TSV delivers the "(a,b,c)" paren-list as a single string cell, which is stripped and split. Shared
 * with the wire-binding transform, which reads the same paren-list shape from a different column.
 */
TArray<FString> ParseQuestKeyList(const FQuestDataValue& Cell);

/**
 * Synthesize the structural prereq form for every convention cell on every node row, then strip the cell so it never
 * reaches RestoreQuestCell as a bogus property. Runs AFTER ReadBundle and BEFORE QuestBundle_Validate, so synthesized endpoints
 * are checked by the ordinary endpoint guard and a typo'd operand key is refused with no half-built asset.
 */
void ApplyQuestFlowConventions(FQuestDataBundle& Bundle, TArray<FString>& Warnings);