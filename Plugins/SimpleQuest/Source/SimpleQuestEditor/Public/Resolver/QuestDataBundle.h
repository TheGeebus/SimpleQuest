// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The ONE neutral bundle both routing halves speak and every format provider produces/consumes. Format-free and
// editor-type-free: no UEdGraph/node types here (that's routing-core territory), no file/format knowledge
// (that's the provider's). ExportRouting emits an FQuestDataBundle from the live graph; a provider WriteBundle
// serializes it; a provider ReadBundle reconstructs it; ImportRouting ingests it.

#include "CoreMinimal.h"
#include "QuestDataValue.h"

// One entity row: its key + structured cells by column name. ABSENT and present-Kind::Empty are DIFFERENT statements: a
// present Empty cell means the source declared the column and left it at its default, while no cell at all means the source
// never had that column. Both leave the destination property untouched on restore, so a consumer that only wants the value
// can ignore the difference — but one that must decide whether the source is asserting a default or saying nothing cannot.
// Get() returns a cell's plain-string value ("" for either case) — the convenience the routing core uses for structural
// columns (class / graph / QuestlineID), which are always string cells.
struct FQuestDataRow
{
	FString Key;
	TMap<FString, FQuestDataValue> Cells;

	// Structural-column string accessor (class / graph / QuestlineID — always plain-string cells). Reads Scalar, the
	// string field every provider populates for a string-valued cell.
	FString Get(const FString& Col) const
	{
		const FQuestDataValue* V = Cells.Find(Col);
		return V ? V->StringForm : FString();
	}
};

// One per-type entity table: the ordered column names (export lays out + guards against these; import parses them from
// the header line) + the rows. No Stem field — the stem is the TablesByType key, the single source of truth.
struct FQuestDataTable
{
	TArray<FString> Columns;
	TArray<FQuestDataRow> Rows;
};

// One typed edge in the unified relationship table. Type carries the parenthesized qualifier (pin name / property path).
struct FQuestDataEdge
{
	FString From;
	FString Type;
	FString To;
};

// Everything a provider produces/consumes and the routing core emits/ingests. TablesByType is keyed by the snake_cased
// short type stem. KnotsCollapsed is a produce-time provenance stat (export sets it for its summary log; import ignores
// it, leaving 0).
struct FQuestDataBundle
{
	TMap<FString, FQuestDataTable> TablesByType;
	TArray<FQuestDataEdge> Edges;
	int32 KnotsCollapsed = 0;
};