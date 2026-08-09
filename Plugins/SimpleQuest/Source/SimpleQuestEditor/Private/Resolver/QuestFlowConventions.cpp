// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestFlowConventions.h"

#include "Resolver/QuestDataBundle.h"
#include "SimpleQuestLog.h"

namespace
{
	// We SYNTHESIZE the exact structural form the graph would have - a combinator row + operand edges in + a feeds-prereq
	// edge out - into the neutral bundle, then the ordinary P0-P5 pipeline compiles it (no new compiler path; combinators
	// store NO operands, the wiring is emergent from edges - same shape Ch6 round-trips green). Routing-core +
	// format-agnostic: identical for TSV/JSON. The convention is a WRITE-IN only; export always emits the explicit
	// structural form (so "graph is sugar" the reverse way - a re-export of an unlock_after import shows prerequisite_and
	// rows, never unlock_after).
	struct FFlowConvention
	{
		const TCHAR* Column;              // authored column name a designer writes
		const TCHAR* CombinatorClass;    // node class to synthesize
		const TCHAR* TableStem;          // == TypeStem(class); the TablesByType key the combinator row lands in
		const TCHAR* OutEdgeType;        // the combinator output pin, as a feeds-prereq verb (AND="Out", OR/NOT="PrereqOut")
		int32        MaxOperands;        // operand cap: AND/OR fan-in freely (INT32_MAX); NOT takes exactly 1
		bool         bHasConditionPinCount; // AND/OR carry a ConditionPinCount cell (variadic pins); NOT's single pin is fixed
	};

	// Vocab. Each convention is the SAME synthesis with a different combinator class + output verb, so extending is data,
	// not new control flow. AND's output pin is "Out"; OR's (and NOT's) is "PrereqOut" - verified vs AllocateDefaultPins
	// + the Ch6 export edges - which is why the output verb is a per-row field rather than hardcoded.
	static const FFlowConvention GFlowConventions[] =
	{
		{ TEXT("unlock_after"),  TEXT("QuestlineNode_PrerequisiteAnd"), TEXT("prerequisite_and"), TEXT("feeds-prereq(Out)"),       MAX_int32, true  },
		{ TEXT("unlock_any"),    TEXT("QuestlineNode_PrerequisiteOr"),  TEXT("prerequisite_or"),  TEXT("feeds-prereq(PrereqOut)"), MAX_int32, true  },
		{ TEXT("unlock_unless"), TEXT("QuestlineNode_PrerequisiteNot"), TEXT("prerequisite_not"), TEXT("feeds-prereq(PrereqOut)"), 1,         false },
	};
}

TArray<FString> ParseQuestKeyList(const FQuestDataValue& Cell)
{
	TArray<FString> Keys;
	if (Cell.Kind == EQuestDataValueKind::Array)
	{
		for (const FQuestDataValue& Elem : Cell.Elements)
		{
			const FString K = Elem.StringForm.TrimStartAndEnd();
			if (!K.IsEmpty()) Keys.Add(K);
		}
		return Keys;
	}
	FString Raw = Cell.StringForm.TrimStartAndEnd();
	Raw.RemoveFromStart(TEXT("("));
	Raw.RemoveFromEnd(TEXT(")"));
	TArray<FString> Parts;
	Raw.ParseIntoArray(Parts, TEXT(","), /*CullEmpty*/ true);
	for (FString& P : Parts) { const FString K = P.TrimStartAndEnd(); if (!K.IsEmpty()) Keys.Add(K); }
	return Keys;
}

void ApplyQuestFlowConventions(FQuestDataBundle& Bundle, TArray<FString>& Warnings)
{
	// Synthesized rows/edges are STAGED into locals and applied AFTER the scan - never insert into Bundle.TablesByType
	// while iterating it (FindOrAdd can rehash + invalidate the outer iterator when the combinator table doesn't yet
	// exist, which is the common case). Stage per-stem so a single new table absorbs every gate of that kind.
	TMap<FString, TArray<FQuestDataRow>> RowsToAddByStem;
	TArray<FQuestDataEdge> EdgesToAdd;
	int32 Synthesized = 0;

	for (TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		if (TablePair.Key == TEXT("questline_graph")) continue;   // the self row can't be gated
		for (FQuestDataRow& Row : TablePair.Value.Rows)
		{
			// A content node's Prerequisites input is single-link (the schema disallows a second wire - combine with
			// AND/OR instead). So at most ONE flow-convention may apply per row; a second populated convention column
			// is a source authoring error. Honor the FIRST (table order), strip + warn the rest - never synthesize two
			// combinators into one input (that can't round-trip and the compiled model would be undefined).
			bool bRowConsumed = false;
			for (const FFlowConvention& Conv : GFlowConventions)
			{
				const FQuestDataValue* Cell = Row.Cells.Find(Conv.Column);
				if (!Cell) continue;

				// Declared but blank on THIS row: the source has the column and this row does not use it. That is the normal
				// shape for a rectangular source - a header or a row struct must declare every gate column used ANYWHERE, so
				// most rows leave most of them blank. It is not a gate, not a conflict with an earlier one, and not a property
				// either, so strip it and move on without a word. Placed ahead of the conflict check deliberately: that branch
				// fires first, and would otherwise accuse a designer of double-declaring a prerequisite they never wrote.
				if (Cell->Kind == EQuestDataValueKind::Empty)
				{
					Row.Cells.Remove(Conv.Column);
					continue;
				}
				
				if (bRowConsumed)
				{
					Warnings.Add(FString::Printf(TEXT("'%s' carries %s in addition to an earlier prerequisite convention - "
						"a node's Prerequisites input takes only one; ignoring %s (combine operands within a single convention)"),
						*Row.Key, Conv.Column, Conv.Column));
					Row.Cells.Remove(Conv.Column);
					continue;
				}

				TArray<FString> Operands = ParseQuestKeyList(*Cell);
				Row.Cells.Remove(Conv.Column);   // strip regardless - a declared-but-empty gate is still not a property
				if (Operands.Num() == 0)
				{
					Warnings.Add(FString::Printf(TEXT("%s on '%s' listed no operands - no prerequisite synthesized"),
						Conv.Column,
						*Row.Key));
					continue;
				}

				// Operand cap: NOT negates exactly one operand. If a source over-lists, keep the first + warn (refusing
				// the whole gate would be harsher than honoring the clear intent of the first operand).
				if (Operands.Num() > Conv.MaxOperands)
				{
					Warnings.Add(FString::Printf(TEXT("%s on '%s' lists %d operands but takes at most %d - using the first, ignoring the rest"),
						Conv.Column, *Row.Key, Operands.Num(), Conv.MaxOperands));
					Operands.SetNum(Conv.MaxOperands);
				}

				// Combinator key: derived from the GATED row + column so re-imports are stable and two gates never
				// collide. Needn't be a GUID - SpawnQuestNodeFromRow mints a deterministic FGuid from any non-GUID key.
				const FString CombKey = Row.Key + TEXT("__") + FString(Conv.Column);
				const FString GraphCell = Row.Get(TEXT("graph"));   // same graph level as the gated node

				FQuestDataRow CombRow;
				CombRow.Key = CombKey;
				CombRow.Cells.Add(TEXT("class"),  FQuestDataValue::MakeString(Conv.CombinatorClass));
				CombRow.Cells.Add(TEXT("graph"),  FQuestDataValue::MakeString(GraphCell));
				// ConditionPinCount only for variadic combinators (AND/OR). NOT's single input pin is fixed at
				// AllocateDefaultPins with no count property - emitting the cell would be a stray column.
				if (Conv.bHasConditionPinCount)
					CombRow.Cells.Add(TEXT("ConditionPinCount"), FQuestDataValue::MakeNumber(FString::FromInt(FMath::Max(Operands.Num(), 2))));
				RowsToAddByStem.FindOrAdd(Conv.TableStem).Add(MoveTemp(CombRow));

				for (const FString& OpKey : Operands)
					EdgesToAdd.Add({ OpKey, TEXT("activates(Any Outcome)"), CombKey });
				EdgesToAdd.Add({ CombKey, FString(Conv.OutEdgeType), Row.Key });

				++Synthesized;
				bRowConsumed = true;
				UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("ImportQuestline: flow-convention %s on '%s' -> %s '%s' gating via %d operand(s)"),
					Conv.Column,
					*Row.Key,
					Conv.CombinatorClass,
					*CombKey,
					Operands.Num());
			}
		}
	}

	// Apply the staged synthesis now that the scan is complete (safe to insert into TablesByType).
	for (TPair<FString, TArray<FQuestDataRow>>& StemPair : RowsToAddByStem)
	{
		FQuestDataTable& CombTable = Bundle.TablesByType.FindOrAdd(StemPair.Key);
		if (CombTable.Columns.Num() == 0 && StemPair.Value.Num() > 0)
		{
			// Seed value columns (WriteBundle prepends "key" itself) in a STABLE order, and only those the rows
			// actually carry - NOT rows omit ConditionPinCount, so a hardcoded list would seed a phantom column that
			// breaks the re-export diff. class/graph are on every combinator row; ConditionPinCount only on variadic ones.
			const FQuestDataRow& Sample = StemPair.Value[0];
			CombTable.Columns = { TEXT("class"), TEXT("graph") };
			if (Sample.Cells.Contains(TEXT("ConditionPinCount")))
				CombTable.Columns.Add(TEXT("ConditionPinCount"));
		}
		for (FQuestDataRow& R : StemPair.Value) CombTable.Rows.Add(MoveTemp(R));
	}
	Bundle.Edges.Append(MoveTemp(EdgesToAdd));

	if (Synthesized > 0)
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: synthesized %d prerequisite combinator(s) from flow conventions."), Synthesized);
}