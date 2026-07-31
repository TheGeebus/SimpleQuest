// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The source-column provider: "give me the columns this mapping source exposes" as one abstraction over two provenances —
// a foreign file read through a format provider, or a Data Table's row struct. Returns only column NAMEs (FName), never the
// neutral bundle types (which stay Private to the routing core). This is what feeds the mapping panel's source-column and
// discriminator-column dropdowns, so a designer selects from real columns instead of typing — the anti-corruption spine.

#include "CoreMinimal.h"

class ISimpleQuestDataFormat;
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

// Distinct raw values found in one column of a foreign-file source — the discriminator column's actual value set, so the
// value->class rows can be PICKED from the data instead of typed. Reads the folder through the format provider and collects
// every distinct non-empty value of ColumnName across all content tables, in first-seen order (stable for the UI). Values
// are returned RAW (un-normalized) — the caller displays them as-authored; matching against the class map normalizes. An
// unreadable source / absent column yields an empty array (the panel shows no value rows, loudly, never typed guesses).
TArray<FString> EnumerateColumnDistinctValues(const FString& FormatName, const FString& SourceFolder, const FName& ColumnName);

// Data Table source: walk the row struct's properties. No file I/O, no parse, cannot be stale (the struct is the authority).
// Both binding sides are FProperty-shaped here — the purest no-typing case. A null/unloadable table yields bReadable=false.
FQuestSourceColumns EnumerateDataTableColumns(const TSoftObjectPtr<UDataTable>& SourceTable);

// Normalize a discriminator VALUE for matching: trim surrounding whitespace + fold case. The ONE definition both the
// validation guard and ApplyMapping use, so a value in the source and a key in the mapping match identically on both
// sides — never two copies that could drift ("objective" vs "objective " silently mis-routing).
FString NormalizeDiscriminatorValue(const FString& Raw);

// Build the discriminator-value -> node class map the guard validates against AND ApplyMapping routes with — ONE builder so
// the two never disagree on membership. Keyed by NormalizeDiscriminatorValue. Reports two authoring-time failures into
// OutErrors: a value whose class won't resolve, and two raw keys that collide after normalization (many-to-one: "Step" and
// "step " both fold to "step", which would silently overwrite). Returns true iff the map is clean (no errors added).
bool BuildDiscriminatorClassMap(const UQuestImportMapping& Mapping, TMap<FString, UClass*>& OutClassByNormValue,
								TArray<FText>& OutErrors);

// The shared mapping-vs-source guard: the ONE rule set both the panel (edit-time, advisory -> readiness UI) and the import
// path (bind-time, refuses -> the structural guarantee) enforce. Returns true iff the mapping can be applied to a source
// exposing ActualColumns and ActualDiscriminatorValues without silent data loss; on false, OutErrors holds one entry per
// problem. Source-agnostic: the panel passes the enumerated/cached source, the import passes the freshly-read actual data.
// OutWarnings (optional) collects NON-FATAL advisories — legibility concerns that don't threaten correctness and so never
// change the return value (e.g. the discriminator column also being bound to a property). Pass nullptr to ignore them.
bool ValidateMappingAgainstSource(const UQuestImportMapping& Mapping, const TArray<FName>& ActualColumns,
								  const TArray<FString>& ActualDiscriminatorValues, TArray<FText>& OutErrors,
								  TArray<FText>* OutWarnings = nullptr);

// Select the format provider for an import/export op: a --format=<name> console arg (highest), else the project default,
// else "TSV". Returns null (and logs the reason under LogPrefix) when a named format isn't registered — the caller refuses.
// One definition shared by both the import and export prototypes; LogPrefix distinguishes their error messages.
TUniquePtr<ISimpleQuestDataFormat> MakeQuestDataFormat(const TArray<FString>& Args, const TCHAR* LogPrefix);
