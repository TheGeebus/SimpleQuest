// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestRowPlanner.h"

#include "SimpleQuestLog.h"
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

	// Recorded by the PLANNER, not its callers: the planner is the one holding the destination, and stamping it before the
	// first refusal means even a plan that dies on the row struct still says which table it died on.
	OutPlan.DestinationAssetPath = Destination.GetPathName();

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
			// Structural, never a destination field. The recipe's KEY COLUMN belongs with them: when the destination
			// has no such field the refusal above already says so, in more useful words, and when it does have one this
			// scan never fires for it.
			if (Cell.Key == TEXT("class") || Cell.Key == TEXT("graph")) continue;
			if (!Mapping.KeyColumn.IsNone() && Cell.Key == Mapping.KeyColumn.ToString()) continue;
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

	// WHERE A ROW LIVES vs WHAT IT IS CALLED. A DataTable's row NAME belongs to the studio; when the recipe names a
	// KeyColumn their semantic key is a CELL. Matching on the row name then finds nothing, every row plans as a Create,
	// and the apply writes a duplicate beside every row they already have - silently, because a Create carries no
	// changes for a reviewer to notice.
	auto ReadKeyCell = [](const FProperty* Prop, const void* RowMemory) -> FString
	{
		const void* Value = Prop->ContainerPtrToValuePtr<void>(RowMemory);
		// Read the two realistic key types directly: ExportTextItem would quote an FString, and a quoted key matches nothing.
		if (const FStrProperty* Str = CastField<FStrProperty>(Prop))     { return Str->GetPropertyValue(Value); }
		if (const FNameProperty* Name = CastField<FNameProperty>(Prop))  { return Name->GetPropertyValue(Value).ToString(); }
		FString Out;
		Prop->ExportTextItem_Direct(Out, Value, nullptr, nullptr, PPF_None);
		return Out;
	};

	const FProperty* KeyProp = nullptr;
	TMap<FString, FName> RowNameByKey;
	if (!Mapping.KeyColumn.IsNone())
	{
		const FString KeyColumnName = Mapping.KeyColumn.ToString();
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			if (RowStruct->GetAuthoredNameForField(*It) == KeyColumnName) { KeyProp = *It; break; }
		}
		if (!KeyProp)
		{
			OutPlan.Refusals.Add(FString::Printf(TEXT("the recipe's key column '%s' is not a field on '%s' - every row "
				"would match nothing and be planned as a duplicate"), *KeyColumnName, *RowStruct->GetName()));
		}
		else
		{
			for (const TPair<FName, uint8*>& Existing : ExistingRows)
			{
				if (!Existing.Value) { continue; }
				const FString Value = ReadKeyCell(KeyProp, Existing.Value);
				if (!Value.IsEmpty()) { RowNameByKey.Add(Value, Existing.Key); }
			}
		}
	}

	for (const FQuestDataRow& Row : Content->Rows)
	{
		if (OutPlan.AmbiguousKeys.Contains(Row.Key)) continue;

		FQuestNodePlanEntry Entry;
		Entry.Key       = Row.Key;
		Entry.ClassName = Mapping.DiscriminatorColumn.IsNone() ? FString() : Row.Get(Mapping.DiscriminatorColumn.ToString());

		// Matched by KEY COLUMN when the recipe names one, by row name otherwise. Deliberately not both: a row whose
		// NAME happens to equal our key while its key CELL says otherwise is somebody else's row, and writing into it
		// on a coincidence is the failure this whole match exists to avoid.
		uint8* const* Existing = nullptr;
		FName RowName;
		if (KeyProp)
		{
			if (const FName* Matched = RowNameByKey.Find(Row.Key))
			{
				RowName  = *Matched;
				Existing = ExistingRows.Find(RowName);
			}
			else
			{
				// A create. Their key becomes the new row's name too - readable, stable, and it stops mattering the
				// moment the key column exists, since nothing matches on the name any more.
				RowName = FName(*Row.Key);
			}
		}
		else
		{
			RowName  = FName(*Row.Key);
			Existing = ExistingRows.Find(RowName);
		}
		Entry.DestinationRowName = RowName;

		if (!Existing || !*Existing)
		{
			// Naming a new row after their key is only safe if that name is free. Taken by a row we did NOT match means
			// it is somebody else's, and AddRow would overwrite it without a word - so this one row is refused rather
			// than the whole plan, the same shape the ambiguity refusal above uses.
			if (KeyProp && ExistingRows.Contains(RowName))
			{
				OutPlan.Refusals.Add(FString::Printf(TEXT("'%s' would be a new row, but a row of that name already "
					"exists carrying a different key - writing it would overwrite theirs"), *Row.Key));
				continue;
			}

			// A row this graph claims that the table does not have yet. Nothing to diff against, so like an inbound
			// Create it carries no changes - the apply writes the whole row.
			Entry.Action = EQuestNodePlanAction::Create;
			OutPlan.Entries.Add(MoveTemp(Entry));
			continue;
		}

		// The SAME comparison rule the inbound direction runs, pointed the other way: the destination row's memory is
		// the live container, and the graph's row is what would be written into it.
		// THE KEY IS NOT PART OF THAT COMPARISON. It is how these two were matched, so it cannot legitimately differ -
		// and left in, it reports once per row as an unmatched column whenever the destination has no such field, which
		// is the refusal above repeated once per row. Stripped HERE rather than taught to the diff-er: that function is
		// the single definition of "a change" for both directions and should not learn a recipe's vocabulary to serve
		// one caller. A Create is unaffected - it does not diff, and apply writes from the source row, not this copy.
		Entry.Action = EQuestNodePlanAction::Update;
		FQuestDataRow ForDiff = Row;
		if (!Mapping.KeyColumn.IsNone()) { ForDiff.Cells.Remove(Mapping.KeyColumn.ToString()); }
		DiffQuestContainerAgainstRow(RowStruct, *Existing, ForDiff, FString(), Entry, OutPlan);
		OutPlan.Entries.Add(MoveTemp(Entry));
	}

	// Everything else in the table belongs to someone else. Counted so the plan can say how much of the table it is NOT
	// talking about - a number a reviewer needs before trusting a write into a shared asset.
	for (const TPair<FName, uint8*>& Existing : ExistingRows)
	{
		// Claimed on the SAME basis the match used, or the count contradicts the plan sitting above it.
		const FString Claimable = (KeyProp && Existing.Value) ? ReadKeyCell(KeyProp, Existing.Value) : Existing.Key.ToString();
		if (!Seen.Contains(Claimable)) { ++OutPlan.UnclaimedRowCount; }
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
	
	// Only when something would ACTUALLY be lost. A table always lacks a home for the questline's own row, so warning
	// on the row's mere existence fires every single time - and a warning that always fires is furniture, which costs
	// more than it saves the first time the banner carries something real. Cells are Empty at their default by
	// contract, so a self row of nothing-but-defaults has nothing to lose.
	if (const FQuestDataTable* Self = Bundle.TablesByType.Find(TEXT("questline_graph")))
	{
		int32 Authored = 0;
		for (const FQuestDataRow& Row : Self->Rows)
		{
			for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
			{
				if (Cell.Key == TEXT("class")) continue;   // structural - describes the row, not authored content
				if (Cell.Value.Kind != EQuestDataValueKind::Empty) { ++Authored; }
			}
		}
		if (Authored > 0)
		{
			OutPlan.Warnings.Add(FString::Printf(TEXT("%d authored questline propert(ies) - QuestlineID, display data, "
				"questline rewards - have no row to be written into and would not reach that table. A table describes "
				"nodes, not the asset holding them."), Authored));
		}
	}
}

