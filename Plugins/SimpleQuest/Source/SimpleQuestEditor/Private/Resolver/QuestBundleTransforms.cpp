// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestBundleTransforms.h"

#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestFlowConventions.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestReflectionUtils.h"
#include "SimpleQuestLog.h"
#include "Utilities/SimpleQuestEditorUtils.h"


bool QuestBundle_Validate(const FQuestDataBundle& Bundle,
                    TMap<FString, const FQuestDataRow*>& NodeRowsByKey,   // node/self key -> row (excludes child rows)
                    TSet<FString>& AllRowKeys,                            // every key incl. instanced child keys
                    FString& OutError)
{
	// The questline-self table is keyed "questline_graph" - required, exactly one row.
	const FQuestDataTable* Questline = Bundle.TablesByType.Find(TEXT("questline_graph"));
	if (!Questline) { OutError = TEXT("no questline_graph table (the self row)"); return false; }
	if (Questline->Rows.Num() != 1) { OutError = TEXT("questline_graph table must have exactly one row"); return false; }

	for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		const bool bIsSelf = (TablePair.Key == TEXT("questline_graph"));
		for (const FQuestDataRow& R : TablePair.Value.Rows)
		{
			// A KEY IS AN IDENTITY. Two rows sharing one are not two statements about a thing, they are two rows each
			// claiming to BE it, and nothing downstream can choose between them: FindQuestChildRow returns the first
			// match while walking a TMap, so which one wins is not even stable across runs. Previously this Add
			// silently swallowed the second row and NodeRowsByKey.Add silently overwrote the first.
			// CHILD ROWS ARE CHECKED TOO, and they are the reason this matters: their keys carry an array index minted
			// from the live array length, so two branches each appending to one array both mint the same index - in
			// different per-class files, where a textual merge sees no overlap at all and joins them cleanly.

			bool bAlreadySeen = false;
			AllRowKeys.Add(R.Key, &bAlreadySeen);
			if (bAlreadySeen)
			{
				OutError = FString::Printf(TEXT("duplicate row key '%s' (seen again in table '%s') - two rows cannot "
					"claim one identity"), *R.Key, *TablePair.Key);
				return false;
			}

			AllRowKeys.Add(R.Key);
			const bool bIsChild = R.Key.Contains(TEXT("/"));
			if (!bIsChild && !bIsSelf) NodeRowsByKey.Add(R.Key, &R);
		}
	}

	// Validate every edge endpoint resolves to a known key (node, self, or child).
	for (const FQuestDataEdge& E : Bundle.Edges)
	{
		if (!AllRowKeys.Contains(E.From)) { OutError = FString::Printf(TEXT("edge 'from' references unknown key: %s"), *E.From); return false; }
		if (!AllRowKeys.Contains(E.To))   { OutError = FString::Printf(TEXT("edge 'to' references unknown key: %s"), *E.To); return false; }
	}

	// Validate exactly one Entry row per graph cell (each graph level has one Entry - the import adopts it).
	TMap<FString, int32> EntryCountByGraph;
	for (const TPair<FString, const FQuestDataRow*>& Pair : NodeRowsByKey)
	{
		const FQuestDataRow* R = Pair.Value;
		if (R->Get(TEXT("class")) == TEXT("QuestlineNode_Entry"))
		{
			EntryCountByGraph.FindOrAdd(R->Get(TEXT("graph")))++;
		}
	}
	for (const TPair<FString, int32>& Pair : EntryCountByGraph)
	{
		if (Pair.Value != 1) { OutError = FString::Printf(TEXT("graph '%s' has %d Entry rows (expected 1)"), *Pair.Key, Pair.Value); return false; }
	}
	return true;
}

// ---- MAPPING (studio source-shape translation) ----------------------------------------------------------------
// A studio's own-shaped source (usually a flat table whose rows are different node kinds, distinguished by a "type"
// column) arrives as one table (ReadQuestBundle keys tables by file). This makes it routable: read the discriminator
// column, look up each row's node class, stamp the row's "class" cell, then rename bound source columns to their
// canonical property names - applying a binding only where the resolved class actually has that property (bind once,
// land where it fits). Runs before ApplyQuestFlowConventions/QuestBundle_Validate so the class-driven pipeline sees canonical
// rows. The questline_graph self table is left untouched (it isn't a fanned-out source table).
bool QuestBundle_ApplyMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
{
	if (Mapping.DiscriminatorColumn.IsNone())
	{
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: mapping has no discriminator column set - malformed mapping, refusing."));
		return false;
	}
	const FString DiscCol = Mapping.DiscriminatorColumn.ToString();

	// Extract the actual source shape from the bundle we're about to transform (no drift - this IS the data), then run
	// the shared guard. BINDING: refuse the whole import on any failure rather than silently drop rows. The extraction
	// must read the discriminator cell BEFORE the routing loop removes it (below).
	TArray<FName> ActualColumns;
	TArray<FString> ActualDiscriminatorValues;
	{
		TSet<FName> ColSet; TSet<FString> ValSet;
		for (const TPair<FString, FQuestDataTable>& TP : Bundle.TablesByType)
		{
			if (TP.Key == TEXT("questline_graph")) continue;
			for (const FString& C : TP.Value.Columns) ColSet.Add(FName(*C));
			for (const FQuestDataRow& R : TP.Value.Rows)
			{
				const FString V = R.Get(DiscCol);
				if (!V.IsEmpty()) ValSet.Add(V);
			}
		}
		ActualColumns = ColSet.Array();
		ActualDiscriminatorValues = ValSet.Array();
	}
	TArray<FText> GuardErrors;
	TArray<FText> GuardWarnings;
	const bool bGuardOK = ValidateMappingAgainstSource(Mapping, ActualColumns, ActualDiscriminatorValues, GuardErrors, &GuardWarnings);
	for (const FText& W : GuardWarnings)   // advisories log regardless of pass/fail - they never block, only inform
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline mapping advisory: %s"), *W.ToString());
	if (!bGuardOK)
	{
		for (const FText& E : GuardErrors)
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline mapping guard: %s"), *E.ToString());
		return false;   // refuse - no partial asset from an unsafe mapping
	}

	// The routing class map - the SAME shared builder the guard used, so membership can't drift. The guard already
	// refused on any build error, so BuildErrors can't fire here. Then cache each class's authored-property set for the
	// per-row bind-where-it-fits check.
	TMap<FString, UClass*> ClassByValue;   // keyed by NormalizeDiscriminatorValue
	TArray<FText> BuildErrors;
	BuildDiscriminatorClassMap(Mapping, ClassByValue, BuildErrors);
	TMap<UClass*, TSet<FName>> PropsByClass;
	for (const TPair<FString, UClass*>& CV : ClassByValue)
	{
		if (!PropsByClass.Contains(CV.Value))
		{
			TSet<FName> Names;
			for (const FName& N : GetAuthoredPropertyNames(CV.Value)) Names.Add(N);
			PropsByClass.Add(CV.Value, MoveTemp(Names));
		}
	}

	int32 Routed = 0;
	for (TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		if (TablePair.Key == TEXT("questline_graph")) continue;   // self row is not a fanned-out source table

		for (FQuestDataRow& Row : TablePair.Value.Rows)
		{
			// 1. Which kind is this row? Read the discriminator cell, resolve its class, stamp the "class" cell.
			const FString DiscValue = Row.Get(DiscCol);
			UClass* const* Found = ClassByValue.Find(NormalizeDiscriminatorValue(DiscValue));
			if (!Found)
			{
				// Unreachable at import (the guard refused any unmapped value); defensive for a guard-less caller.
				Warnings.Add(FString::Printf(TEXT("mapping: row '%s' has %s='%s' with no class mapping - row not routed"),
					*Row.Key, *DiscCol, *DiscValue));
				continue;
			}
			UClass* RowClass = *Found;
			Row.Cells.Add(TEXT("class"), FQuestDataValue::MakeString(RowClass->GetName()));   // short name (TryFindTypeSlow form)
			Row.Cells.Remove(DiscCol);   // the discriminator column isn't a node property
			const TSet<FName>& RowProps = PropsByClass[RowClass];

			// 2. Rename each bound source column to its canonical property name - but only when this class has that
			//    property (bind once, land where it fits). A binding that doesn't fit this class is simply skipped.
			for (const FQuestColumnBinding& B : Mapping.Bindings)
			{
				if (!RowProps.Contains(B.TargetProperty)) continue;   // property not on this class - binding doesn't apply
				const FString SrcCol = B.SourceColumn.ToString();
				FQuestDataValue* Cell = Row.Cells.Find(SrcCol);
				if (!Cell) continue;   // source column absent on this row - the absent-policy is handled downstream
				FQuestDataValue Moved = MoveTemp(*Cell);
				Row.Cells.Remove(SrcCol);
				Row.Cells.Add(B.TargetProperty.ToString(), MoveTemp(Moved));
			}
			++Routed;
		}
	}
	if (Routed > 0)
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: mapping routed + renamed %d row(s)."), Routed);
	return true;
}

// ---- WIRE BINDINGS (studio-declared row-adjacent relationships) ------------------------------------------------
// A studio's flat source expresses flow as a COLUMN of target keys ("next": n_exit) rather than an edge table. Each
// wire-binding names such a column plus the edge kind it means; we synthesize the identical {From,Type,To} edges the
// edge table would have carried, then STRIP the cell so it never reaches RestoreQuestCell as a bogus property (same
// contract as the flow conventions below). An EMPTY qualifier emits "verb()", which ResolveQuestSourcePin reads as "this
// node's default output of that kind" - so one binding wires Entry, Step and Exit alike. Mapping-only: it's studio
// vocabulary translation, so it never runs on our own round-trip.
void QuestBundle_ApplyWireBindings(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
{
	if (Mapping.WireBindings.Num() == 0) return;

	int32 Synthesized = 0;
	for (TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		if (TablePair.Key == TEXT("questline_graph")) continue;   // the self row wires nothing

		for (FQuestDataRow& Row : TablePair.Value.Rows)
		{
			for (const FQuestWireBinding& Wire : Mapping.WireBindings)
			{
				if (Wire.SourceColumn.IsNone()) continue;
				const FString Col = Wire.SourceColumn.ToString();
				const FQuestDataValue* Cell = Row.Cells.Find(Col);
				if (!Cell) continue;                                   // column absent on this row - nothing to wire

				const TArray<FString> Targets = ParseQuestKeyList(*Cell);
				Row.Cells.Remove(Col);                                 // strip: a wire column is not a node property
				if (Targets.Num() == 0) continue;                      // present but empty == no wiring, not an error

				const FString EdgeType = FString::Printf(TEXT("%s(%s)"), *Wire.EdgeVerb.ToString(), *Wire.Qualifier);
				for (const FString& Target : Targets)
				{
					Bundle.Edges.Add({ Row.Key, EdgeType, Target });
					++Synthesized;
				}
			}
		}
	}
	if (Synthesized > 0)
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: synthesized %d edge(s) from wire binding(s)."), Synthesized);
}

/**
 * Which wire binding, if any, does this edge belong to? Qualified bindings are tested FIRST so a named-outcome column is
 * never swallowed by the default-flow one. An unqualified binding then claims an edge only when its pin is the from-node's
 * DEFAULT output for that verb — the same rule the import applies forward, read backwards.
 */
static const FQuestWireBinding* FindClaimingWireBinding(const FQuestDataEdge& Edge, const UQuestImportMapping& Mapping,
                                                 const TMap<FString, const UQuestlineNodeBase*>& NodeByGuid)
{
	FString Verb = Edge.Type, Pin;
	int32 Open, Close;
	if (Edge.Type.FindChar(TEXT('('), Open) && Edge.Type.FindLastChar(TEXT(')'), Close) && Close > Open)
	{
		Verb = Edge.Type.Left(Open);
		Pin  = Edge.Type.Mid(Open + 1, Close - Open - 1);
	}

	for (const FQuestWireBinding& W : Mapping.WireBindings)
	{
		if (W.SourceColumn.IsNone() || W.Qualifier.IsEmpty()) continue;
		if (W.EdgeVerb.ToString() != Verb) continue;
		// The pin's real name is the full tag; the panel offers the outcome LABEL. Accept either, as the import does.
		if (W.Qualifier == Pin) return &W;
		if (FSimpleQuestEditorUtilities::GetOutcomeLabel(FName(*Pin)).ToString() == W.Qualifier) return &W;
	}

	const UQuestlineNodeBase* const* Found = NodeByGuid.Find(Edge.From);
	if (!Found || !*Found) return nullptr;
	const FName WantCategory = FSimpleQuestEditorUtilities::PinCategoryForEdgeVerb(Verb);
	if (WantCategory.IsNone()) return nullptr;

	const UEdGraphPin* DefaultPin = nullptr;
	for (UEdGraphPin* P : (*Found)->Pins)
	{
		if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == WantCategory) { DefaultPin = P; break; }
	}
	if (!DefaultPin || DefaultPin->PinName.ToString() != Pin) return nullptr;

	for (const FQuestWireBinding& W : Mapping.WireBindings)
	{
		if (W.SourceColumn.IsNone() || !W.Qualifier.IsEmpty()) continue;
		if (W.EdgeVerb.ToString() == Verb) return &W;
	}
	return nullptr;
}

/**
 * Restate a row key in the studio's vocabulary. A CHILD key is "<owner>/<property path>", and only the OWNER segment is
 * a node identity — the rest is a path we author and they never see. Restating the owner alone keeps a child addressable
 * by the name its owner now answers to; leaving it addresses a row that no longer exists. Nesting is handled for free,
 * because a grandchild's owner segment is still the leading path component.
 */
static FString RestateKey(const FString& Key, const TMap<FString, FString>& SourceKeyByGuid)
{
	int32 SlashIdx;
	if (Key.FindChar(TEXT('/'), SlashIdx))
	{
		const FString Owner = Key.Left(SlashIdx);
		if (const FString* Mapped = SourceKeyByGuid.Find(Owner)) { return *Mapped + Key.Mid(SlashIdx); }
		return Key;
	}
	if (const FString* Mapped = SourceKeyByGuid.Find(Key)) { return *Mapped; }
	return Key;
}

// ---- REVERSE MAPPING (restate the canonical bundle in the STUDIO's vocabulary) ---------------------------------
// The inverse read of the same recipe the import uses forward: our class -> their discriminator value, our property
// names -> their column names, and the per-type tables merged back into the ONE mixed table their source actually was.
// Runs on the bundle the exporter just built, immediately before serialization — no second graph walk.
// This is a faithful RE-STATEMENT, not a regeneration: per-row casing, values equal to our defaults, and the studio's
// original file arrangement are not recoverable from the graph.
void QuestBundle_ApplyReverseMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, const TMap<FString, FString>& SourceKeyByGuid, const TMap<FString, const UQuestlineNodeBase*>& NodeByGuid, TArray<FString>& Warnings)
{
	if (Mapping.DiscriminatorColumn.IsNone())
	{
		Warnings.Add(TEXT("reverse mapping: the recipe has no discriminator column — bundle left in canonical shape"));
		return;
	}

	// Our class name -> the value the studio calls it. PrimaryValue is the authored choice when one class answers to
	// several values; otherwise the first (and usually only) value.
	TMap<FString, FString> ValueByClassName;
	for (const FQuestDiscriminatorClass& Entry : Mapping.DiscriminatorClasses)
	{
		const UClass* Cls = Entry.NodeClass.LoadSynchronous();
		if (!Cls || Entry.Values.Num() == 0) continue;
		ValueByClassName.Add(Cls->GetName(), Entry.PrimaryValue.IsEmpty() ? Entry.Values[0] : Entry.PrimaryValue);
	}

	const FString DiscCol = Mapping.DiscriminatorColumn.ToString();
	FQuestDataTable Flat;
	TSet<FString> FlatCols;
	auto AddCol = [&](const FString& Col) { if (!FlatCols.Contains(Col)) { FlatCols.Add(Col); Flat.Columns.Add(Col); } };
	AddCol(DiscCol);   // the discriminator leads, as it does in a studio's own table

	// Claimed edges are recorded but NOT removed yet — the removal happens only after every one of them has actually
	// become a cell (see the invariant after the row walk). Removing up front would make a mismatch unrecoverable.
	TMap<FString, TMap<FString, TArray<FString>>> WireCellsByRow;   // from-guid -> column -> target keys
	TSet<int32> ClaimedEdges;
	int32 WiresClaimed = 0;
	int32 WiresWritten = 0;
	if (Mapping.WireBindings.Num() > 0)
	{
		for (int32 i = 0; i < Bundle.Edges.Num(); ++i)
		{
			const FQuestDataEdge& Edge = Bundle.Edges[i];
			const FQuestWireBinding* Claim = FindClaimingWireBinding(Edge, Mapping, NodeByGuid);
			if (!Claim) continue;
			WireCellsByRow.FindOrAdd(Edge.From)
						  .FindOrAdd(Claim->SourceColumn.ToString())
						  .Add(RestateKey(Edge.To, SourceKeyByGuid));
			ClaimedEdges.Add(i);
			++WiresClaimed;
		}
	}

	int32 Restated = 0;
	TMap<FString, FQuestDataTable> RetainedChildTables;   // instanced children keep their own tables; only node rows flatten
	for (TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		if (TablePair.Key == TEXT("questline_graph")) continue;   // the self row keeps its own shape

		FQuestDataTable Retained;
		Retained.Columns = TablePair.Value.Columns;

		for (FQuestDataRow& Row : TablePair.Value.Rows)
		{
			// An instanced CHILD row is OURS, not theirs. A studio's flat source has no way to express one, so the recipe
			// says nothing about its vocabulary and there is nothing to translate — collapsing it into their content file
			// would hand them rows they never wrote, with columns that pollute the header. Its KEY still has to follow its
			// owner into the studio's namespace or it addresses a row that no longer exists, but the row itself stays
			// canonical, in its own table.
			if (Row.Key.Contains(TEXT("/")))
			{
				Row.Key = RestateKey(Row.Key, SourceKeyByGuid);
				Retained.Rows.Add(MoveTemp(Row));
				continue;
			}

			// A. class cell -> their discriminator value. Our "class" marker is not part of their vocabulary.
			const FString ClassName = Row.Get(TEXT("class"));
			if (const FString* Value = ValueByClassName.Find(ClassName))
			{
				Row.Cells.Remove(TEXT("class"));
				Row.Cells.Add(DiscCol, FQuestDataValue::MakeString(*Value));
			}
			else if (!ClassName.IsEmpty())
			{
				Warnings.Add(FString::Printf(TEXT("reverse mapping: no discriminator value maps class '%s' — "
					"those rows keep the canonical class cell"), *ClassName));
			}

			// B. our property names -> their column names (the same Bindings list, read right-to-left).
			for (const FQuestColumnBinding& B : Mapping.Bindings)
			{
				if (B.SourceColumn.IsNone() || B.TargetProperty.IsNone()) continue;
				const FString Prop = B.TargetProperty.ToString();
				if (FQuestDataValue* Cell = Row.Cells.Find(Prop))
				{
					FQuestDataValue Moved = MoveTemp(*Cell);
					Row.Cells.Remove(Prop);
					Row.Cells.Add(B.SourceColumn.ToString(), MoveTemp(Moved));
				}
			}

			// C. Wiring this row declares, written as their column(s). MUST run before D — WireCellsByRow is keyed by the
			//     exported GUID, and D is about to replace Row.Key with the studio's key. A single target is written bare;
			//     several use the same paren list the import parses, so the value round-trips through ParseQuestKeyList.
			if (const TMap<FString, TArray<FString>>* Cols = WireCellsByRow.Find(Row.Key))
			{
				for (const TPair<FString, TArray<FString>>& ColPair : *Cols)
				{
					const FString Value = ColPair.Value.Num() == 1
						? ColPair.Value[0]
						: FString::Printf(TEXT("(%s)"), *FString::Join(ColPair.Value, TEXT(",")));
					Row.Cells.Add(ColPair.Key, FQuestDataValue::MakeString(Value));
					WiresWritten += ColPair.Value.Num();
				}
			}

			// D. Their key, not our GUID. Import preserved the studio's row key on the node when it wasn't already one of
			//    our GUIDs; restoring it is what makes the file readable as a diff against their source. A CHILD key
			//    carries its owner as a leading path segment, so it goes through the same helper rather than a bare lookup.
			Row.Key = RestateKey(Row.Key, SourceKeyByGuid);

			// E. The LEVEL a row sits in, restated the same way. The graph cell names its owning container by OUR exported
			//     GUID, while D has just renamed that container's row to the studio's key — so left alone, a nested file
			//     refers to a level no row declares. That is not cosmetic: import matches a child to its level by comparing
			//     this cell against the container's ROW KEY, so a mismatch makes every nested row silently vanish on the way
			//     back in. The top level is "root", which names no node and passes through untouched.
			if (FQuestDataValue* GraphCell = Row.Cells.Find(TEXT("graph")))
			{
				if (const FString* SourceLevel = SourceKeyByGuid.Find(GraphCell->StringForm))
				{
					GraphCell->StringForm = *SourceLevel;
				}
			}

			// F. Text as plain text. Import turned their plain string into an FText and INVENTED a localization key for
			//    it — a different one every run — so writing the full NSLOCTEXT form back would hand them machinery they
			//    never authored and that churns on every export. Emit what they wrote.
			for (TPair<FString, FQuestDataValue>& Cell : Row.Cells)
			{
				if (Cell.Value.Kind == EQuestDataValueKind::Text)
				{
					Cell.Value = FQuestDataValue::MakeString(Cell.Value.Text.ToString());
				}
			}

			for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells) { AddCol(Cell.Key); }
			Flat.Rows.Add(MoveTemp(Row));
			++Restated;
		}

		if (!Retained.Rows.IsEmpty()) { RetainedChildTables.Add(TablePair.Key, MoveTemp(Retained)); }
	}

	// INVARIANT — every relationship taken out of the edge table must have become a cell. If a claimed edge's from-row
	// never appeared in the bundle, dropping the edge while writing no column would silently DELETE that relationship:
	// the failure this pass is most able to cause and least able to show, since the export would still look well-formed.
	// On mismatch, keep the edge table intact and strip the partial columns, so the wiring is still expressed exactly
	// once — canonically rather than in their vocabulary — and nothing is lost.
	if (WiresClaimed == WiresWritten)
	{
		TArray<FQuestDataEdge> KeptEdges;
		for (int32 i = 0; i < Bundle.Edges.Num(); ++i)
		{
			if (!ClaimedEdges.Contains(i)) { KeptEdges.Add(Bundle.Edges[i]); }
		}
		Bundle.Edges = MoveTemp(KeptEdges);
	}
	else
	{
		for (FQuestDataRow& R : Flat.Rows)
		{
			for (const FQuestWireBinding& W : Mapping.WireBindings)
			{
				if (!W.SourceColumn.IsNone()) { R.Cells.Remove(W.SourceColumn.ToString()); }
			}
		}
		Warnings.Add(FString::Printf(TEXT("reverse mapping: %d relationship(s) matched a wire binding but only %d became "
			"columns — the wiring has been left in the edge table rather than the studio's columns, so none is lost."),
			WiresClaimed, WiresWritten));
	}

	// 3. Drop columns that carry nothing. Everything the recipe does NOT bind still arrives here under OUR property name
	//    — that is our vocabulary leaking into their file, and it is what breaks a diff against their own source. A
	//    column at its default on every row says nothing, so it goes. One that DOES carry a value stays (under our name),
	//    so unmapped data is VISIBLE rather than silently dropped. The discriminator and every mapped column are always
	//    kept: they are part of the studio's shape whether or not this particular export populated them.
	TSet<FString> Keep;
	Keep.Add(DiscCol);
	for (const FQuestColumnBinding& B : Mapping.Bindings)
	{
		if (!B.SourceColumn.IsNone()) { Keep.Add(B.SourceColumn.ToString()); }
	}
	for (const FQuestWireBinding& W : Mapping.WireBindings)
	{
		if (!W.SourceColumn.IsNone()) { Keep.Add(W.SourceColumn.ToString()); }   // part of their shape even when unused here
	}
	for (const FQuestDataRow& Row : Flat.Rows)
	{
		for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
		{
			if (Cell.Value.Kind != EQuestDataValueKind::Empty) { Keep.Add(Cell.Key); }
		}
	}
	const int32 ColsBefore = Flat.Columns.Num();
	Flat.Columns.RemoveAll([&Keep](const FString& Col) { return !Keep.Contains(Col); });
	const int32 ColsAfter = Flat.Columns.Num();
	for (FQuestDataRow& Row : Flat.Rows)
	{
		TArray<FString> Drop;
		for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
		{
			if (!Keep.Contains(Cell.Key)) { Drop.Add(Cell.Key); }
		}
		for (const FString& D : Drop) { Row.Cells.Remove(D); }
	}

	// 4. Collapse the per-type tables into the single mixed table their source was. The self row is untouched.
	FQuestDataTable SelfTable;
	const bool bHasSelf = Bundle.TablesByType.Contains(TEXT("questline_graph"));
	if (bHasSelf) { SelfTable = MoveTemp(Bundle.TablesByType[TEXT("questline_graph")]); }
	Bundle.TablesByType.Empty();
	if (bHasSelf) { Bundle.TablesByType.Add(TEXT("questline_graph"), MoveTemp(SelfTable)); }
	Bundle.TablesByType.Add(TEXT("content"), MoveTemp(Flat));
	for (TPair<FString, FQuestDataTable>& Child : RetainedChildTables)
	{
		Bundle.TablesByType.Add(Child.Key, MoveTemp(Child.Value));
	}

	// 5. Edge endpoints reference rows by key, so they must follow the same substitution — otherwise the wiring points at
	//    GUIDs that no longer appear in any row.
	for (FQuestDataEdge& Edge : Bundle.Edges)
	{
		Edge.From = RestateKey(Edge.From, SourceKeyByGuid);
		Edge.To   = RestateKey(Edge.To,   SourceKeyByGuid);   // a contains edge's target is a CHILD key, not a bare GUID
	}

	UE_LOG(LogSimpleQuestResolver, Log, TEXT("ExportQuestline: restated %d row(s) in the recipe's vocabulary, %d column(s) kept of %d "
		"(faithful re-statement — per-row casing, at-default values and original file layout are not reconstructed), %d wire(s) written."),
		Restated,
		ColsAfter,
		ColsBefore,
		WiresWritten);
}

