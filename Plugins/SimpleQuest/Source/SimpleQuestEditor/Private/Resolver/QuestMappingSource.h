// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The source-column provider: "give me the columns this mapping source exposes" as one abstraction over two provenances —
// a foreign file read through a format provider, or a Data Table's row struct. Returns only column NAMEs (FName), never the
// neutral bundle types (which stay Private to the routing core). This is what feeds the mapping panel's source-column and
// discriminator-column dropdowns, so a designer selects from real columns instead of typing — the anti-corruption spine.

#include "CoreMinimal.h"

class UDataTable;
class UQuestImportMapping;

// Result of a column enumeration: the columns found, plus whether the source was readable and whether it is ambiguous.
// The caller (panel or the import guard) refuses/warns on !bReadable or bHasDuplicateColumns rather than binding blindly.
struct FQuestSourceColumns
{
	TArray<FName> Columns;				// the source's value columns (never includes the structural "key" column)
	bool bReadable = false;				// false = the source couldn't be read (bad folder / unloadable table / bad format)
	bool bHasDuplicateColumns = false;	// true = a column name appears more than once in one source table — data is
										//        ALREADY ambiguous (cells collapsed on parse); a blocking source-validity error
	FText Error;						// human-readable reason when !bReadable or bHasDuplicateColumns
};

// Foreign-file source: read the folder through the named format provider and collect the UNION of value columns across all
// content tables (the self-row "questline_graph" table is excluded — it isn't a fanned-out source of node rows). A column
// name repeated within one table's header is flagged bHasDuplicateColumns (the parse already collapsed its cells, so the
// data is ambiguous). Runs a real ReadBundle — call it on source-descriptor change / Refresh, never per-frame.
FQuestSourceColumns EnumerateForeignFileColumns(const FString& FormatName, const FString& SourceFolder);

// Data Table source: walk the row struct's properties. No file I/O, no parse, cannot be stale (the struct is the authority).
// Both binding sides are FProperty-shaped here — the purest no-typing case. A null/unloadable table yields bReadable=false.
FQuestSourceColumns EnumerateDataTableColumns(const TSoftObjectPtr<UDataTable>& SourceTable);

// Dispatch on the mapping's SourceKind to the matching provenance. One call for a caller that just has a mapping.
FQuestSourceColumns EnumerateMappingSourceColumns(const UQuestImportMapping& Mapping);
