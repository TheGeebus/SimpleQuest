// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestRowPlanner.h"

#include "Engine/DataTable.h"
#include "Resolver/QuestBundleDiff.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestInPlacePlan.h"
#include "UObject/UnrealType.h"


void PlanQuestRowsIntoTable(const FQuestDataBundle& Bundle, const UDataTable& Destination,
							const UQuestImportMapping& Mapping, FQuestInPlacePlan& OutPlan)
{
	OutPlan.Direction = EQuestPlanDirection::IntoTable;

	const UScriptStruct* RowStruct = Destination.GetRowStruct();
	if (!RowStruct)
	{
		OutPlan.Refusals.Add(FString::Printf(TEXT("'%s' has no row struct - there is no layout to write into"),
			*Destination.GetName()));
		return;
	}

	// The flat studio-vocabulary table is the ONLY thing a row write consumes. Its absence means reverse mapping never
	// ran, which would otherwise be discovered one row at a time as a wall of unmatched columns.
	const FQuestDataTable* Content = Bundle.TablesByType.Find(TEXT("content"));
	if (!Content)
	{
		OutPlan.Refusals.Add(TEXT("this bundle has no flat 'content' table - it was never restated in the destination's "
			"vocabulary, so there is nothing addressable to write"));
		return;
	}

	// Asked ONCE against the destination rather than once per row, because a column either fits the struct or never will.
	TSet<FString> StructColumns;
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
	{
		StructColumns.Add(RowStruct->GetAuthoredNameForField(*It));
	}
	TSet<FString> BoundColumns;
	for (const FQuestColumnBinding& Binding : Mapping.Bindings)
	{
		BoundColumns.Add(Binding.SourceColumn.ToString());
	}

	// A column with nowhere to land splits by WHOSE claim it is. One the recipe BOUND is a disagreement between the
	// recipe and the destination - the author said this column carries a property and the struct has no such field, so
	// the write would drop it silently. One that merely passed through under our own name is the destination declining
	// to model something, which is theirs to decline. Refuse the first, report the second.
	TSet<FString> MissingBound;
	TSet<FString> MissingUnbound;
	for (const FQuestDataRow& Row : Content->Rows)
	{
		for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
		{
			if (Cell.Key == TEXT("class") || Cell.Key == TEXT("graph")) continue;   // structural, never a destination field
			if (StructColumns.Contains(Cell.Key)) continue;
			(BoundColumns.Contains(Cell.Key) ? MissingBound : MissingUnbound).Add(Cell.Key);
		}
	}
	for (const FString& Column : MissingBound)
	{
		OutPlan.Refusals.Add(FString::Printf(TEXT("the recipe binds column '%s', which '%s' has no field for - every "
			"value written through it would be dropped"), *Column, *RowStruct->GetName()));
	}
	for (const FString& Column : MissingUnbound)
	{
		OutPlan.Warnings.Add(FString::Printf(TEXT("column '%s' has no field on '%s' and would not be written - the "
			"destination does not model it"), *Column, *RowStruct->GetName()));
	}

	// Two nodes answering to one key would plan two writes to one row, the second silently winning. Refused by key, the
	// same shape the inbound direction uses, so neither claimant is acted on rather than one being picked by iteration order.
	TSet<FString> Seen;
	for (const FQuestDataRow& Row : Content->Rows)
	{
		if (Seen.Contains(Row.Key)) { OutPlan.AmbiguousKeys.AddUnique(Row.Key); }
		Seen.Add(Row.Key);
	}
	for (const FString& Key : OutPlan.AmbiguousKeys)
	{
		OutPlan.Warnings.Add(FString::Printf(TEXT("'%s' names more than one row - that row is not planned. Clear the "
			"import provenance on the duplicate to resolve it."), *Key));
	}

	const TMap<FName, uint8*>& ExistingRows = Destination.GetRowMap();

	for (const FQuestDataRow& Row : Content->Rows)
	{
		if (OutPlan.AmbiguousKeys.Contains(Row.Key)) continue;

		FQuestNodePlanEntry Entry;
		Entry.Key       = Row.Key;
		Entry.ClassName = Mapping.DiscriminatorColumn.IsNone() ? FString() : Row.Get(Mapping.DiscriminatorColumn.ToString());

		uint8* const* Existing = ExistingRows.Find(FName(*Row.Key));
		if (!Existing || !*Existing)
		{
			// A row this graph claims that the table does not have yet. Nothing to diff against, so like an inbound
			// Create it carries no changes - the apply writes the whole row.
			Entry.Action = EQuestNodePlanAction::Create;
			OutPlan.Entries.Add(MoveTemp(Entry));
			continue;
		}

		// The SAME comparison rule the inbound direction runs, pointed the other way: the destination row's memory is
		// the live container, and the graph's row is what would be written into it.
		Entry.Action = EQuestNodePlanAction::Update;
		DiffQuestContainerAgainstRow(RowStruct, *Existing, Row, FString(), Entry, OutPlan);
		OutPlan.Entries.Add(MoveTemp(Entry));
	}

	// Everything else in the table belongs to someone else. Counted so the plan can say how much of the table it is NOT
	// talking about - a number a reviewer needs before trusting a write into a shared asset.
	for (const TPair<FName, uint8*>& Existing : ExistingRows)
	{
		if (!Seen.Contains(Existing.Key.ToString())) { ++OutPlan.UnclaimedRowCount; }
	}

	// A flat row struct cannot hold these, and the reverse mapping deliberately leaves them out of the content table.
	// Reported rather than refused: it is a limit of the DESTINATION, not a fault in the graph - but silence here is
	// exactly the invisible data loss this whole arc exists to prevent.
	if (Bundle.Edges.Num() > 0)
	{
		OutPlan.Warnings.Add(FString::Printf(TEXT("%d relationship(s) no column binding covers cannot be written to a "
			"flat table and would be lost"), Bundle.Edges.Num()));
	}
	int32 ChildTables = 0;
	for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
	{
		if (Table.Key != TEXT("content") && Table.Key != TEXT("questline_graph")) { ChildTables += Table.Value.Rows.Num(); }
	}
	if (ChildTables > 0)
	{
		OutPlan.Warnings.Add(FString::Printf(TEXT("%d nested object row(s) have no destination in a flat row struct and "
			"would be lost"), ChildTables));
	}
	if (Bundle.TablesByType.Contains(TEXT("questline_graph")))
	{
		OutPlan.Warnings.Add(TEXT("the questline's own properties have no row to be written into - a table describes "
			"nodes, not the asset holding them"));
	}
}

