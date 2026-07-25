// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// PROTOTYPE — Resolver, Phase 3 Stage 2. The ONE neutral bundle both routing halves speak and every format provider
// produces/consumes. Format-free and editor-type-free: no UEdGraph/node types here (that's routing-core territory), no
// file/format knowledge (that's the provider's). ExportRouting emits an FQuestDataBundle from the live graph; a provider
// WriteBundle serializes it; a provider ReadBundle reconstructs it; ImportRouting ingests it. See
// notes-07-phase3-stage2-code-spec.txt §2.

#include "CoreMinimal.h"
#include "QuestDataValue.h"

// One entity row: its key + structured cells by column name. A column may be ABSENT (== Kind::Empty / a value left at
// its CDO default); consumers treat absent and present-Empty identically. Get() returns the cell's default-format string
// (CanonicalText) — the convenience import uses for structural columns (class / graph / QuestlineID), which are always
// Kind::Scalar and read as plain strings.
struct FQuestDataRow
{
	FString Key;
	TMap<FString, FQuestDataValue> Cells;

	// Structural-column string accessor (class / graph / QuestlineID — always plain-string cells). Reads Scalar, which
	// BOTH providers populate (TSV mirrors it into CanonicalText too; JSON sets only Scalar). Reading Scalar rather than
	// CanonicalText keeps this working for structured providers and aligns with the Stage-4 direction (CanonicalText, the
	// TSV-only crutch, is going away — Scalar is the durable string field).
	FString Get(const FString& Col) const
	{
		const FQuestDataValue* V = Cells.Find(Col);
		return V ? V->Scalar : FString();
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