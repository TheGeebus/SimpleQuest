// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// PROTOTYPE - Resolver, Phase 2 import (the round-trip's second half). Reconstructs AUTHORED editor nodes from the
// interlingua table folder an export produced, then feeds the EXISTING compiler - never reverses the compiler. Creates
// a FRESH asset (QuestlineID suffixed _RT so its compiled tag namespace doesn't collide with the original), so the
// round-trip is verifiable by the two oracles: C (re-export this asset, diff the folders modulo _RT) and B2
// (compile + DumpCompiled both, diff modulo the tag prefix). Console-triggered, editor-only. Not shipped API.

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/DataTable.h"
#include "Factories/QuestlineGraphFactory.h"
#include "Graph/QuestlineGraphSchema.h"
#include "IAssetTools.h"
#include "Internationalization/Text.h"
#include "ISimpleQuestEditorModule.h"
#include "Misc/Paths.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestBundleTransforms.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestDataValueBuilder.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestInPlacePlan.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Resolver/QuestReflectionUtils.h"
#include "SimpleQuestLog.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Utilities/QuestlineGraphCompiler.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	// The import routing core speaks the shared, format-free FQuestDataBundle (Resolver/QuestDataBundle.h). The local
	// FImport* bundle structs + the TSV parsing (Unsanitize / ParseTable / ParseEdges) moved to the TSV provider
	// (Resolver/TsvQuestDataFormat::ReadBundle) in Stage 2 - the routing core never touches a file or format. What was
	// ReadAndValidate splits: the provider reads the folder into a bundle; ValidateBundle (below) does the structural
	// checks on that already-parsed bundle.

	// P0 (routing half) - structural validation of an ALREADY-READ bundle. File reading (folder discovery + TSV parse)
	// is the provider's job (ReadBundle); this does ONLY the structural checks and builds the two lookup indices the
	// later phases need. Refuse (return false) on any inconsistency so no partial asset is ever created (validate-
	// upfront). Provider-agnostic: a malformed bundle from ANY format provider is refused here identically.
	bool ValidateBundle(const FQuestDataBundle& Bundle,
	                    TMap<FString, const FQuestDataRow*>& NodeRowsByKey,   // node/self key -> row (excludes child rows)
	                    TSet<FString>& AllRowKeys,                            // every key incl. instanced child keys
	                    FString& OutError)
	{
		// The questline-self table is keyed "questline_graph" - required, exactly one row.
		const FQuestDataTable* Questline = Bundle.TablesByType.Find(TEXT("questline_graph"));
		if (!Questline) { OutError = TEXT("no questline_graph table (the self row)"); return false; }
		if (Questline->Rows.Num() != 1) { OutError = TEXT("questline_graph table must have exactly one row"); return false; }

		// Index every row key. Node/self rows are keyed by GUID digits or the EffectiveID; instanced child rows carry
		// a '/' path segment. Only NODE rows spawn editor nodes, so split the two - but track ALL keys so edge
		// endpoints that legitimately reference child rows (contains edges) validate. Self = the questline_graph table.
		for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
		{
			const bool bIsSelf = (TablePair.Key == TEXT("questline_graph"));
			for (const FQuestDataRow& R : TablePair.Value.Rows)
			{
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

	// ---- FLOW CONVENTIONS (prereq-from-table) ---------------------------------------------------------------------
	// Write-in authoring sugar: a flat source (no edge/flow notion) declares a prereq as a COLUMN of operand row-keys
	// (e.g. unlock_after:(step_a,step_b)) instead of hand-wiring a combinator. We SYNTHESIZE the exact structural form
	// the graph would have - a combinator row + operand edges in + a feeds-prereq edge out - into the neutral bundle,
	// then the ordinary P0-P5 pipeline compiles it (no new compiler path; combinators store NO operands, the wiring is
	// emergent from edges - same shape Ch6 round-trips green). Routing-core + format-agnostic: identical for TSV/JSON.
	// The convention is a WRITE-IN only; export always emits the explicit structural form (so "graph is sugar" the
	// reverse way - a re-export of an unlock_after import shows prerequisite_and rows, never unlock_after).
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

	// Parse an operand-key list from a convention cell. A structured provider (JSON) delivers Kind::Array (use the
	// elements); TSV delivers the "(a,b,c)" paren-list literal as one string cell (strip parens, split on comma). Empty
	// entries are dropped; the caller warns on a wholly-empty list.
	TArray<FString> ParseFlowKeyList(const FQuestDataValue& Cell)
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

	// ---- MAPPING (studio source-shape translation) ----------------------------------------------------------------
	// A studio's own-shaped source (usually a flat table whose rows are different node kinds, distinguished by a "type"
	// column) arrives as one table (ReadBundle keys tables by file). This makes it routable: read the discriminator
	// column, look up each row's node class, stamp the row's "class" cell, then rename bound source columns to their
	// canonical property names - applying a binding only where the resolved class actually has that property (bind once,
	// land where it fits). Runs before ApplyFlowConventions/ValidateBundle so the class-driven pipeline sees canonical
	// rows. The questline_graph self table is left untouched (it isn't a fanned-out source table).
	bool ApplyMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
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

	// Synthesize the structural prereq form for every convention cell found on any node row, then strip the cell so it
	// never reaches RestoreCell as a bogus property. Runs on the bundle AFTER ReadBundle and BEFORE ValidateBundle, so
	// the synthesized edges' endpoints are checked by the ordinary endpoint guard (a typo'd operand key is refused with
	// no half-built asset). PrerequisiteAnd has a floor of 2 condition pins, so ConditionPinCount = max(N, 2) - a
	// single operand wires one pin and leaves one free (legal; ResolveDestPin only fills FREE condition pins).
	void ApplyFlowConventions(FQuestDataBundle& Bundle, TArray<FString>& Warnings)
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

					TArray<FString> Operands = ParseFlowKeyList(*Cell);
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
					// collide. Needn't be a GUID - SpawnNodeFromRow mints a deterministic FGuid from any non-GUID key.
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

	// ---- WIRE BINDINGS (studio-declared row-adjacent relationships) ------------------------------------------------
	// A studio's flat source expresses flow as a COLUMN of target keys ("next": n_exit) rather than an edge table. Each
	// wire-binding names such a column plus the edge kind it means; we synthesize the identical {From,Type,To} edges the
	// edge table would have carried, then STRIP the cell so it never reaches RestoreCell as a bogus property (same
	// contract as the flow conventions below). An EMPTY qualifier emits "verb()", which ResolveSourcePin reads as "this
	// node's default output of that kind" - so one binding wires Entry, Step and Exit alike. Mapping-only: it's studio
	// vocabulary translation, so it never runs on our own round-trip.
	void ApplyWireBindings(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
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

					const TArray<FString> Targets = ParseFlowKeyList(*Cell);
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

	void RestoreCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value);   // fwd decl (Array recurses)

	// Restore a Kind=Array cell into the destination container. The Kind erases container type (TArray vs TSet), so branch
	// on the destination Prop. Each element recurses through RestoreCell by ITS own Kind. NOTE: this path is exercised only
	// by a STRUCTURED provider (JSON); TSV delivers arrays as a single Kind=Scalar cell (the "(a,b)" literal -> ImportText),
	// so TSV never reaches here - no regression risk. FGameplayTagContainer arriving as an Array (a JSON tag list) is also
	// handled: a TagContainer destination isn't an FArray/FSetProperty, so it falls to the ImportText fallback on the
	// element-joined literal - but in practice JSON emits FGameplayTagContainer as Kind=TagContainer, not Array.
	void RestoreArrayCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value)
	{
		// A structured provider (JSON) delivers an FGameplayTagContainer as a Kind=Array of tag-string elements. The
		// destination property's type decides: a container gets the elements requested as tags + assigned directly (the
		// wrapped "(GameplayTags=..)" ImportText form can't be reconstructed from bare tag strings, so type it here).
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
			{
				FGameplayTagContainer Container;
				for (const FQuestDataValue& Elem : Value.Elements)
				{
					const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Elem.StringForm), false);
					if (Tag.IsValid()) Container.AddTag(Tag);
				}
				*static_cast<FGameplayTagContainer*>(ValuePtr) = Container;
				return;
			}
		}
		if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrProp, ValuePtr);
			Helper.EmptyValues();
			for (const FQuestDataValue& Elem : Value.Elements)
			{
				const int32 Idx = Helper.AddValue();
				RestoreCell(ArrProp->Inner, Helper.GetRawPtr(Idx), Elem);
			}
			return;
		}
		if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			FScriptSetHelper Helper(SetProp, ValuePtr);
			Helper.EmptyElements();
			for (const FQuestDataValue& Elem : Value.Elements)
			{
				const int32 Idx = Helper.AddDefaultValue_Invalid_NeedsRehash();
				RestoreCell(SetProp->ElementProp, Helper.GetElementPtr(Idx), Elem);
			}
			Helper.Rehash();
			return;
		}
		// Unexpected destination for an Array Kind - leave default (defensive; a structured provider shouldn't emit this).
	}

	// Restore one property from a structured cell value. switch(Kind): a STRUCTURED provider (JSON) delivers typed Kinds
	// (Tag/Text/Bool/Array) that write directly to the property from the typed field; the string-carrying Kinds (Scalar/
	// Enum/Reference/StructLiteral) go through ImportText from Value.StringForm. The TSV provider produces Kind=Scalar for
	// EVERY cell (including FText cells, which arrive as a Scalar holding the NSLOCTEXT string), so the Scalar arm MUST
	// preserve the property-type FText branch (ReadFromBuffer) that the pre-Stage-3 code used - otherwise TSV FText cells
	// regress from ReadFromBuffer to ImportText. This keeps the refactor byte-identical for TSV (the B2 no-regression gate).
	void RestoreCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value)
	{
		switch (Value.Kind)
		{
		case EQuestDataValueKind::Empty:
			return;   // leave the constructed default (Q6 symmetry - replaces the old CellText.IsEmpty() skip)

		case EQuestDataValueKind::Tag:
			// A cell's Kind and its destination are paired by NAME alone - a mapping binds any column onto any property and
			// nothing type-checks the pair - so "it is a struct" does not justify the cast. Writing a tag over an unrelated
			// struct is type confusion, and a caller restoring onto an exactly-sized buffer turns the larger write into a heap
			// overflow. Same identity test the array path applies. A mismatch is an authoring error: report it, change nothing.
			if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				if (StructProp->Struct == TBaseStructure<FGameplayTag>::Get())
				{
					*static_cast<FGameplayTag*>(ValuePtr) = Value.Tag;   // typed; inverse of the export reinterpret read
					return;
				}
			}
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("RestoreCell: a tag value was bound to '%s', which is not an FGameplayTag - left at its default."),
				*Prop->GetName());
			return;

		case EQuestDataValueKind::TagContainer:
			if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
				{
					*static_cast<FGameplayTagContainer*>(ValuePtr) = Value.TagContainer;
					return;
				}
			}
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("RestoreCell: a tag-container value was bound to '%s', which is not an FGameplayTagContainer - left at its default."),
				*Prop->GetName());
			return;

		case EQuestDataValueKind::Text:
			if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
			{
				TextProp->SetPropertyValue(ValuePtr, Value.Text);   // typed - carries loc ns/key, no buffer round-trip
			}
			return;

		case EQuestDataValueKind::Bool:
			if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				BoolProp->SetPropertyValue(ValuePtr, Value.bBool);
			}
			return;

		case EQuestDataValueKind::Array:
			RestoreArrayCell(Prop, ValuePtr, Value);   // container-type branch handled inside (array/set); recurses
			return;

		case EQuestDataValueKind::Number:
		case EQuestDataValueKind::String:
		case EQuestDataValueKind::Enum:
		case EQuestDataValueKind::Reference:
		case EQuestDataValueKind::StructLiteral:
		default:
			{
			// String-carrying Kinds - all route through StringForm -> ImportText, which types against the property (a
			// numeric property parses "3" fine; the Number/String distinction is a provider-render concern, not a
			// restore concern). Value.StringForm holds the cell string (== today's CellText for the TSV path). The FText
			// special-case is PRESERVED here because TSV delivers FText cells as Kind=Scalar (a Scalar holding NSLOCTEXT);
			// routing them to ImportText instead of ReadFromBuffer would regress TSV. JSON never lands here for FText (it
			// uses Kind=Text above), so this branch only ever sees TSV's FText-as-Scalar - exactly the old behavior.
			if (Value.StringForm.IsEmpty()) return;
			if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
			{
				FText Parsed;
				const TCHAR* Buffer = *Value.StringForm;
				if (FTextStringHelper::ReadFromBuffer(Buffer, Parsed))
				{
					TextProp->SetPropertyValue(ValuePtr, Parsed);
				}
				return;
			}
			Prop->ImportText_Direct(*Value.StringForm, ValuePtr, /*OwnerObject*/ nullptr, PPF_None);
			return;
		}
		}
	}

	// Apply every cell in Row to Target's matching UPROPERTY by column name. Skips structural columns (key/class/graph)
	// and any column with no matching property (defensive - a stale table column shouldn't abort the import).
	void RestoreRowProperties(UObject* Target, const FQuestDataRow& Row)
	{
		for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
		{
			const FString& Col = Cell.Key;
			if (Col == TEXT("class") || Col == TEXT("graph")) continue;
			FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*Col));
			if (!Prop) continue;
			// RestoreCell types the value against the property (structured providers write typed fields directly; string-
			// carrying Kinds route through ImportText on Scalar) - see the switch(Kind) in RestoreCell.
			RestoreCell(Prop, Prop->ContainerPtrToValuePtr<void>(Target), Cell.Value);
		}
	}

	const FQuestDataRow* FindChildRow(const FQuestDataBundle& Bundle, const FString& ChildKey, FString& OutClass)
	{
		for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
		{
			for (const FQuestDataRow& R : TablePair.Value.Rows)
			{
				if (R.Key == ChildKey) { OutClass = R.Get(TEXT("class")); return &R; }
			}
		}
		return nullptr;
	}

	// The one reattach primitive, used by both the self row (QuestlineRewards) and per-node rows (Rewards).
	// PROPERTY-DRIVEN (D2): walk Owner's instanced-bearing properties, rebuild each child from its child row (matched
	// by key), in array/map order. The child KEY carries the position (Owner already knows the property + container
	// type from reflection), so the only parse is extracting the trailing [index] / [mapkey] segment. Records every
	// child key it consumed into OutConsumed so P-final can cross-check against the contains edges (D1's completeness
	// property, kept as a tripwire rather than the reconstruction path).
	void ReattachInstanced(UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle,
						   TSet<FString>& OutConsumed, TArray<FString>& OutWarnings);

	// Resolve a class named by a bundle cell. The file carries SHORT names deliberately - they are what a studio reads and
	// diffs, and qualifying them would trade the format's legibility for the reader's convenience. But UClass::TryFindTypeSlow
	// captures AND SYMBOLICATES a ten-frame stack walk for every short name it is handed (CoreUObject's deprecation nudge),
	// which on a corpus import is a per-row cost rather than a one-off. Try the qualified form against the packages our own
	// types live in first; anything else - an adopter's module, a Blueprint-generated "<Name>_C", a full /Game/... path -
	// falls through to exactly the behaviour that was there before.
	UClass* ResolveBundleClass(const FString& ClassName)
	{
		if (ClassName.IsEmpty()) return nullptr;

		if (!FPackageName::IsShortPackageName(ClassName))
		{
			if (UClass* Direct = FindObject<UClass>(nullptr, *ClassName)) return Direct;
			return LoadObject<UClass>(nullptr, *ClassName);
		}

		static const TCHAR* ScriptPackages[] = { TEXT("/Script/SimpleQuest"), TEXT("/Script/SimpleQuestEditor"), TEXT("/Script/SimpleCore") };
		for (const TCHAR* Package : ScriptPackages)
		{
			if (UClass* Found = FindObject<UClass>(nullptr, *FString::Printf(TEXT("%s.%s"), Package, *ClassName))) return Found;
		}

		if (UClass* Found = UClass::TryFindTypeSlow<UClass>(ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous)) return Found;
		return LoadObject<UClass>(nullptr, *ClassName);
	}

	// Rebuild one instanced child object from its row: NewObject<class> under Owner, restore its cells, recurse its
	// own instanced properties. Returns the constructed object (or null if the row/class is missing).
	UObject* BuildChildObject(UObject* Owner, const FString& ChildKey, const FQuestDataBundle& Bundle,
							  TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
	{
		FString ClassName;
		const FQuestDataRow* Row = FindChildRow(Bundle, ChildKey, ClassName);
		if (!Row) { OutWarnings.Add(FString::Printf(TEXT("child row missing for key '%s'"), *ChildKey)); return nullptr; }

		UClass* Class = ResolveBundleClass(ClassName);
		if (!Class)
		{
			OutWarnings.Add(FString::Printf(TEXT("could not resolve child class '%s' for key '%s'"), *ClassName, *ChildKey));
			return nullptr;
		}
		if (!Class)
		{
			// Blueprint-generated adapters serialize as "<Name>_C"; TryFindTypeSlow handles those too, but a fully
			// qualified /Game/... path (if a future export writes one) would come through LoadObject.
			Class = LoadObject<UClass>(nullptr, *ClassName);
		}
		if (!Class)
		{
			OutWarnings.Add(FString::Printf(TEXT("could not resolve child class '%s' for key '%s'"), *ClassName, *ChildKey));
			return nullptr;
		}

		UObject* Sub = NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional);
		RestoreRowProperties(Sub, *Row);
		OutConsumed.Add(ChildKey);
		ReattachInstanced(Sub, ChildKey, Bundle, OutConsumed, OutWarnings);   // a child could itself nest
		return Sub;
	}

	// Parse a child key's LAST path segment: "<owner>/<prop>[<pos>]" or "<owner>/<prop>[<mapkey>].<sub>[<pos>]".
	// Returns the leaf property name + the bracketed token; the middle path is already resolved because we arrive here
	// via the property walk, not by parsing the whole path (D2's smaller-parser property).
	bool SplitLeafSegment(const FString& ChildKey, FString& OutBracketToken)
	{
		int32 OpenIdx;
		if (!ChildKey.FindLastChar(TEXT('['), OpenIdx)) return false;
		int32 CloseIdx;
		if (!ChildKey.FindLastChar(TEXT(']'), CloseIdx) || CloseIdx < OpenIdx) return false;
		OutBracketToken = ChildKey.Mid(OpenIdx + 1, CloseIdx - OpenIdx - 1);
		return true;
	}

	void ReattachInstanced(UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle,
						   TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
	{
		for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			// Only authored instanced-bearing properties produced child rows on export (same filter shape).
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)) continue;

			const FString PropPrefix = FString::Printf(TEXT("%s/%s"), *OwnerKey, *Prop->GetName());

			// Array of instanced objects: rebuild elements in [i] order (children whose key starts with "<owner>/<prop>[").
			if (FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
			{
				FObjectProperty* InnerObj = CastField<FObjectProperty>(Arr->Inner);
				if (!InnerObj || !Arr->Inner->HasAnyPropertyFlags(CPF_InstancedReference)) continue;

				// Gather this property's child keys, ordered by numeric index.
				TArray<TPair<int32, FString>> Indexed;
				for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
					for (const FQuestDataRow& R : TablePair.Value.Rows)
						if (R.Key.StartsWith(PropPrefix + TEXT("[")))
						{
							FString Tok; SplitLeafSegment(R.Key, Tok);
							Indexed.Add({ FCString::Atoi(*Tok), R.Key });
						}
				Indexed.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B){ return A.Key < B.Key; });

				// SILENCE IS NOT AN ASSERTION OF EMPTINESS. A source that declares no children for this property has said
				// nothing about it - the same contract a missing scalar cell carries, where RestoreCell's Empty arm leaves the
				// constructed value alone. Clearing here would make silence destructive: restoring onto an owner that already
				// holds authored children would discard them for no reason the source ever gave. Declaring children still
				// replaces the contents wholesale, which is the source stating what they are.
				if (Indexed.IsEmpty()) continue;

				FScriptArrayHelper Helper(Arr, Prop->ContainerPtrToValuePtr<void>(Owner));
				Helper.EmptyValues();
				for (const TPair<int32, FString>& Pair : Indexed)
				{
					const int32 NewIdx = Helper.AddValue();
					if (UObject* Child = BuildChildObject(Owner, Pair.Value, Bundle, OutConsumed, OutWarnings))
						InnerObj->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), Child);
				}
				continue;
			}

			// Map<key, struct-wrapping-instanced-array>: the QuestlineRewards shape. Rebuild by re-adding each map
			// entry (key parsed from the child path's map segment) then recursing the struct's inner array.
			if (FMapProperty* Map = CastField<FMapProperty>(Prop))
			{
				// Child keys look like "<owner>/QuestlineRewards[<mapkey>].Rewards[i]". Group by the <mapkey> segment.
				TSet<FString> MapKeyTokens;
				const FString MapOpen = PropPrefix + TEXT("[");
				for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
					for (const FQuestDataRow& R : TablePair.Value.Rows)
						if (R.Key.StartsWith(MapOpen))
						{
							// extract the FIRST bracket token (the map key), which the export wrote as ExportTextItem(key).
							int32 Open, Close;
							R.Key.FindChar(TEXT('['), Open);
							R.Key.FindChar(TEXT(']'), Close);
							if (Close > Open) MapKeyTokens.Add(R.Key.Mid(Open + 1, Close - Open - 1));
						}

				if (MapKeyTokens.IsEmpty()) continue;   // see the array case: an unmentioned property is not an empty one

				FScriptMapHelper Helper(Map, Prop->ContainerPtrToValuePtr<void>(Owner));
				Helper.EmptyValues();
				for (const FString& KeyTok : MapKeyTokens)
				{
					const int32 Pair = Helper.AddDefaultValue_Invalid_NeedsRehash();
					// Import the map KEY from its exported text (e.g. a FGameplayTag struct literal).
					Map->KeyProp->ImportText_Direct(*KeyTok, Helper.GetKeyPtr(Pair), nullptr, PPF_None);
					// Recurse the VALUE struct's instanced array. The value's "owner key" for the recursion is the
					// full "<owner>/QuestlineRewards[<keytok>]" prefix so its inner Rewards[i] children resolve.
					const FString ValueOwnerKey = FString::Printf(TEXT("%s[%s]"), *PropPrefix, *KeyTok);
					// The struct value isn't a UObject, so recurse its FStructProperty fields inline:
					if (FStructProperty* ValStruct = CastField<FStructProperty>(Map->ValueProp))
					{
						for (TFieldIterator<FProperty> SIt(ValStruct->Struct); SIt; ++SIt)
						{
							if (FArrayProperty* InnerArr = CastField<FArrayProperty>(*SIt))
							{
								FObjectProperty* InnerObj = CastField<FObjectProperty>(InnerArr->Inner);
								if (!InnerObj || !InnerArr->Inner->HasAnyPropertyFlags(CPF_InstancedReference)) continue;

								const FString ArrPrefix = FString::Printf(TEXT("%s.%s"), *ValueOwnerKey, *SIt->GetName());
								TArray<TPair<int32, FString>> Indexed;
								for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
									for (const FQuestDataRow& R : TablePair.Value.Rows)
										if (R.Key.StartsWith(ArrPrefix + TEXT("[")))
										{ FString Tok; SplitLeafSegment(R.Key, Tok); Indexed.Add({ FCString::Atoi(*Tok), R.Key }); }
								Indexed.Sort([](const auto& A, const auto& B){ return A.Key < B.Key; });
								
								if (Indexed.IsEmpty()) continue;   // see the array case

								FScriptArrayHelper AH(InnerArr, SIt->ContainerPtrToValuePtr<void>(Helper.GetValuePtr(Pair)));
								AH.EmptyValues();
								for (const TPair<int32, FString>& P : Indexed)
								{
									const int32 NewIdx = AH.AddValue();
									if (UObject* Child = BuildChildObject(Owner, P.Value, Bundle, OutConsumed, OutWarnings))
										InnerObj->SetObjectPropertyValue(AH.GetRawPtr(NewIdx), Child);
								}
							}
						}
					}
				}
				Helper.Rehash();
				continue;
			}
		}
	}

	// Spawn one editor node of the row's class into TargetGraph, adopt the exported identity, restore properties +
	// instanced children. GUID preservation = assignment order (Finalize regenerates; we overwrite after). Returns the
	// node, and maps its exported key -> the live node so P3/P4 can resolve edges + do the pin pass.
	UEdGraphNode* SpawnNodeFromRow(UEdGraph* TargetGraph, const FQuestDataRow& Row, const FQuestDataBundle& Bundle,
	                               TMap<FString, UEdGraphNode*>& NodeByKey, TSet<FString>& Consumed, TArray<FString>& Warnings)
	{
		const FString ClassName = Row.Get(TEXT("class"));
		UClass* Class = ResolveBundleClass(ClassName);
		if (!Class) { Warnings.Add(FString::Printf(TEXT("unknown node class '%s' for key '%s'"), *ClassName, *Row.Key)); return nullptr; }

		UEdGraphNode* Node = NewObject<UEdGraphNode>(TargetGraph, Class, NAME_None, RF_Transactional);
		TargetGraph->AddNode(Node, /*bFromUI*/ false, /*bSelectNewNode*/ false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();

		// Adopt identity AFTER the placement hooks (which regenerate GUID + sweep the label). Dual-key contract: a row
		// key that parses as a GUID is one of OUR exports - preserve it verbatim (round-trip identity). A key that does
		// NOT parse is a fresh-authoring semantic id (a studio's "kill_boss") - mint a DETERMINISTIC GUID from it so the
		// identity is stable across re-imports (save data + cross-asset refs + in-place round-trip don't churn).
		// NewDeterministicGuid is a pure name-based hash (no process/build state), so the same id always mints the same
		// GUID. Edge wiring is unaffected: NodeByKey is keyed by the row-key STRING, never this GUID.
		if (UQuestlineNodeBase* QNode = Cast<UQuestlineNodeBase>(Node))
		{
			FGuid ParsedGuid;
			if (FGuid::Parse(Row.Key, ParsedGuid))
			{
				QNode->QuestGuid = ParsedGuid;   // round-trip: preserve our exported GUID verbatim
				// (A GUID key came from our own export; there's no studio-semantic key to preserve - leave ImportSourceKey empty.)
			}
			else
			{
				// Fresh authoring: mint a stable GUID from the semantic key, namespaced so it can't collide with a
				// different consumer's deterministic GUIDs.
				QNode->QuestGuid = FGuid::NewDeterministicGuid(FString(TEXT("SimpleQuest.Import.")) + Row.Key);
				// Preserve the original key STRING so reverse-export can write it back verbatim (the GUID hash isn't invertible).
				QNode->ImportSourceKey = Row.Key;
			}
		}
		RestoreRowProperties(Node, Row);

		TSet<FString> LocalConsumed;
		ReattachInstanced(Node, Row.Key, Bundle, LocalConsumed, Warnings);
		Consumed.Append(LocalConsumed);

		NodeByKey.Add(Row.Key, Node);
		return Node;
	}

	// Remove schema-seeded default nodes (the auto-Entry) from a freshly-created graph so it's populated purely from
	// exported rows. Safe: CreateInnerGraph / the factory only use the default Entry as a starting affordance - nothing
	// retains it; the graph's Entry is always re-found by class (verified: CreateInnerGraph holds no ref, callers scan
	// Graph->Nodes for the Entry type). Schema + (for inner graphs) the change subscription are unaffected.
	void ClearDefaultNodes(UEdGraph* Graph)
	{
		if (!Graph) return;
		TArray<UEdGraphNode*> ToRemove = Graph->Nodes;   // copy - RemoveNode mutates the array
		for (UEdGraphNode* N : ToRemove)
		{
			if (N) Graph->RemoveNode(N);
		}
	}

	// Import every node row belonging to one graph level (graph cell). Quest containers, once spawned + Finalized,
	// have auto-created inner graphs whose auto-Entry we adopt by GUID-overwrite from that inner graph's Entry row -
	// then recurse into the inner level. GraphCell is "root" for the top graph, else the container node's key.
	void ImportGraphLevel(UEdGraph* TargetGraph, const FString& GraphCell, const FQuestDataBundle& Bundle,
						  const TMap<FString, const FQuestDataRow*>& NodeRowsByKey,
						  TMap<FString, UEdGraphNode*>& NodeByKey, TSet<FString>& Consumed, TArray<FString>& Warnings)
	{
		ClearDefaultNodes(TargetGraph);   // populate entirely from rows (incl. the exported Entry) - no double-Entry

		for (const auto& Pair : NodeRowsByKey)
		{
			const FQuestDataRow* Row = Pair.Value;
			if (Row->Get(TEXT("graph")) != GraphCell) continue;

			UEdGraphNode* Node = SpawnNodeFromRow(TargetGraph, *Row, Bundle, NodeByKey, Consumed, Warnings);
			if (!Node) continue;

			if (UQuestlineNode_Quest* Quest = Cast<UQuestlineNode_Quest>(Node))
			{
				UEdGraph* Inner = Quest->GetInnerGraph();   // exists post-Finalize (PostPlacedNewNode -> CreateInnerGraph)
				if (!Inner) { Warnings.Add(FString::Printf(TEXT("container '%s' has no inner graph"), *Row->Key)); continue; }
				ImportGraphLevel(Inner, Row->Key, Bundle, NodeRowsByKey, NodeByKey, Consumed, Warnings);
			}
		}
	}

	// After all nodes exist + properties are restored, regenerate property-derived pins by calling each node type's
	// refresh hook. ORDER MATTERS: a container's outcome pins derive from its inner graph's Exits, so inner graphs must
	// be refreshed before their containers. We achieve innermost-first by processing nodes in descending graph DEPTH
	// (depth = how many container-key hops from root). LinkedQuestline derives from the on-disk linked asset (order-
	// independent). Step/Entry derive from their own restored properties (order-independent).
	int32 GraphDepthOf(const FQuestDataRow* Row, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey)
	{
		int32 Depth = 0;
		FString Cell = Row->Get(TEXT("graph"));
		while (Cell != TEXT("root") && !Cell.IsEmpty())
		{
			++Depth;
			const FQuestDataRow* const* Parent = NodeRowsByKey.Find(Cell);
			if (!Parent) break;
			Cell = (*Parent)->Get(TEXT("graph"));
		}
		return Depth;
	}

	void RefreshPinsPass(const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey,
	                     const TMap<FString, UEdGraphNode*>& NodeByKey, TArray<FString>& Warnings)
	{
		// Order node keys by descending depth (innermost graphs first).
		TArray<FString> Keys;
		NodeByKey.GetKeys(Keys);
		Keys.Sort([&](const FString& A, const FString& B)
		{
			const FQuestDataRow* const* RA = NodeRowsByKey.Find(A);
			const FQuestDataRow* const* RB = NodeRowsByKey.Find(B);
			const int32 DA = RA ? GraphDepthOf(*RA, NodeRowsByKey) : 0;
			const int32 DB = RB ? GraphDepthOf(*RB, NodeRowsByKey) : 0;
			return DA > DB;   // deeper first
		});

		for (const FString& Key : Keys)
		{
			UEdGraphNode* Node = NodeByKey[Key];
			
			// Optional deactivation pins. AllocateDefaultPins (at spawn) creates the "Deactivated" output only when
			// bShowDeactivationPins is true - but that ran BEFORE the property restore, so it was skipped. Both content
			// nodes AND Entry nodes carry this flag (on different classes) and both create the pin the same way. Create
			// it here for any node whose restored flag is true but lacks the pin. Content nodes ALSO get the paired
			// "Deactivate" INPUT (via EnsureDeactivationPinsForAutowire, which handles both); Entry has only the output.
			if (UQuestlineNode_ContentBase* Content = Cast<UQuestlineNode_ContentBase>(Node))
			{
				if (Content->bShowDeactivationPins && !Content->FindPin(TEXT("Deactivated")))
				{
					Content->bShowDeactivationPins = false;       // satisfy the method's already-shown guard
					Content->EnsureDeactivationPinsForAutowire(); // creates Deactivate input + Deactivated output, re-sets flag
				}
			}
			else if (UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(Node))
			{
				if (EntryNode->bShowDeactivationPins && !EntryNode->FindPin(TEXT("Deactivated")))
				{
					EntryNode->CreatePin(EGPD_Output, TEXT("QuestDeactivated"), TEXT("Deactivated"));
				}
			}
			
			if (UQuestlineNode_Step* Step = Cast<UQuestlineNode_Step>(Node))
			{
				Step->RefreshOutcomePins();   // <- DiscoverObjectivePaths(ObjectiveClass), restored from the row
			}
			else if (UQuestlineNode_LinkedQuestline* Linked = Cast<UQuestlineNode_LinkedQuestline>(Node))
			{
				Linked->RebuildOutcomePinsFromLinkedGraph();   // <- linked asset on disk (LinkedGraph restored)
			}
			else if (UQuestlineNode_Quest* Quest = Cast<UQuestlineNode_Quest>(Node))
			{
				Quest->RebuildOutcomePinsFromInnerGraph();   // <- inner Exits; inner graph already refreshed (deeper-first)
			}
			else if (UQuestlineNode_Entry* Entry = Cast<UQuestlineNode_Entry>(Node))
			{
				Entry->RefreshOutcomePins();   // <- restored IncomingSignals; BuildDisambiguatedPinName regenerates names
			}
			else if (UQuestlineNode_PrerequisiteAnd* And = Cast<UQuestlineNode_PrerequisiteAnd>(Node))
			{
				And->SyncConditionPins();   // rebuild Condition_N pins to the restored ConditionPinCount
			}
			else if (UQuestlineNode_PrerequisiteOr* Or = Cast<UQuestlineNode_PrerequisiteOr>(Node))
			{
				Or->SyncConditionPins();
			}
		}
	}

	UEdGraphPin* ResolveSourcePin(UEdGraphNode* Node, const FString& EdgeType)
	{
		// EdgeType is "verb(PinName)" - split it. An EMPTY PinName means "the node's default output of this verb's kind",
		// which is how a studio's row-adjacent wire column ("next") addresses Entry/Step/Exit uniformly without knowing
		// that each names its forward pin differently.
		FString Verb = EdgeType;
		FString PinName;
		int32 Open, Close;
		if (EdgeType.FindChar(TEXT('('), Open) && EdgeType.FindLastChar(TEXT(')'), Close) && Close > Open)
		{
			Verb = EdgeType.Left(Open);
			PinName = EdgeType.Mid(Open + 1, Close - Open - 1);
		}

		if (!PinName.IsEmpty())
		{
			// Exact pin name first - that's what our own export writes, so the round-trip is untouched.
			for (UEdGraphPin* Pin : Node->Pins)
				if (Pin && Pin->Direction == EGPD_Output && Pin->PinName.ToString() == PinName) return Pin;

			// Then the OUTCOME LABEL form: an outcome pin's name is the full tag, but a studio authors (and the mapping
			// panel offers) the namespace-stripped label. Computing the same label from each pin makes the two agree,
			// and keeping the authored sub-hierarchy means "Combat.Won" never collides with "Social.Won".
			for (UEdGraphPin* Pin : Node->Pins)
				if (Pin && Pin->Direction == EGPD_Output
					&& FSimpleQuestEditorUtilities::GetOutcomeLabel(Pin->PinName).ToString() == PinName) return Pin;

			return nullptr;
		}

		// Unqualified: first output pin of the verb's category - the node's primary forward wire by pin-creation order.
		const FName WantCategory = FSimpleQuestEditorUtilities::PinCategoryForEdgeVerb(Verb);
		if (WantCategory.IsNone()) return nullptr;
		for (UEdGraphPin* Pin : Node->Pins)
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == WantCategory) return Pin;
		return nullptr;
	}

	// The DESTINATION input category isn't always the source output category. Outcome outputs (QuestOutcome) route
	// into a target's activation-style input (QuestActivation) - an Exit's "Outcome" pin, or a content node's
	// "Activate". Prereq and activation and deactivation wires keep their category across the wire. Map source
	// category -> the category the destination exposes for that wire kind.
	FName ResolveDestCategory(FName SourceCategory)
	{
		// Outcome outputs route into activation-style inputs; deactivation OUTPUTS (past-tense "QuestDeactivated")
		// route into deactivation INPUTS (present-tense "QuestDeactivate"). Activation + prerequisite match across.
		if (SourceCategory == TEXT("QuestOutcome"))      return TEXT("QuestActivation");
		if (SourceCategory == TEXT("QuestDeactivated"))  return TEXT("QuestDeactivate");
		return SourceCategory;
	}

	// Resolve the DESTINATION input pin. Driven by the DESTINATION NODE'S SHAPE, not by mapping from the source
	// category - because the graph legitimately connects across categories: a Step's QuestActivation "Any Outcome"
	// output AND a NOT's QuestPrerequisite "PrereqOut" output can BOTH feed a combinator's QuestPrerequisite
	// Condition_N input (UE's schema permits it - that's how "step completion satisfies a prereq condition" is
	// authored). Priority:
	//   1. A prereq Condition_N input (combinators): ANY source category routes here - take the first free slot.
	//   2. Else the single input of the source-derived category (outcome/activation -> Activate; prereq -> a
	//      Prerequisites input; deactivate -> Deactivate).
	UEdGraphPin* ResolveDestPin(UEdGraphNode* Node, FName SourceCategory)
	{
		// 1. Combinator condition input - category-agnostic on the source side (a prereq input accepts outcome,
		//    activation, or prereq outputs). First free Condition_N (order-free: no per-slot semantics).
		for (UEdGraphPin* Pin : Node->Pins)
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == TEXT("QuestPrerequisite")
				&& Pin->PinName.ToString().StartsWith(TEXT("Condition_")) && Pin->LinkedTo.Num() == 0)
				return Pin;

		// 2. Non-combinator: the single input matching the wire kind the source category implies.
		const FName DestCategory = ResolveDestCategory(SourceCategory);
		for (UEdGraphPin* Pin : Node->Pins)
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == DestCategory) return Pin;
		return nullptr;
	}

	void WireEdges(const FQuestDataBundle& Bundle, const TMap<FString, UEdGraphNode*>& NodeByKey,
	               const TSet<FString>& ConsumedChildKeys, TArray<FString>& Warnings)
	{
		for (const FQuestDataEdge& E : Bundle.Edges)
		{
			// contains edges are NOT wiring - they're the instanced-reattach record. Cross-check (D1's completeness
			// property, kept as a tripwire): every contains edge's child must have been consumed by the property walk.
			if (E.Type.StartsWith(TEXT("contains")))
			{
				// Two distinct "contains" kinds share the verb: contains(InnerGraph) is a container->inner-NODE edge
				// (those nodes are spawned by ImportGraphLevel, not reattached), while contains(<prop>[i]) is an
				// instanced sub-object child (reattached by ReattachInstanced -> ConsumedChildKeys). The cross-check
				// only applies to the latter; InnerGraph edges are handled by the graph-level spawn and must be skipped.
				const bool bInstancedChild = !E.Type.Contains(TEXT("contains(InnerGraph)"));
				if (bInstancedChild && !ConsumedChildKeys.Contains(E.To))
					Warnings.Add(FString::Printf(TEXT("contains edge child '%s' was NOT reattached by the property walk "
						"(edge/property asymmetry)"), *E.To));
				continue;
			}

			UEdGraphNode* const* FromNode = NodeByKey.Find(E.From);
			UEdGraphNode* const* ToNode = NodeByKey.Find(E.To);
			if (!FromNode || !ToNode)
			{
				Warnings.Add(FString::Printf(TEXT("edge endpoint not spawned: %s -> %s"), *E.From, *E.To));
				continue;
			}
			UEdGraphPin* SourcePin = ResolveSourcePin(*FromNode, E.Type);
			if (!SourcePin) { Warnings.Add(FString::Printf(TEXT("no source pin for edge %s %s"), *E.From, *E.Type)); continue; }
			UEdGraphPin* DestPin = ResolveDestPin(*ToNode, SourcePin->PinType.PinCategory);
			if (!DestPin) { Warnings.Add(FString::Printf(TEXT("no dest pin for edge -> %s (cat %s)"), *E.To, *SourcePin->PinType.PinCategory.ToString())); continue; }

			SourcePin->MakeLinkTo(DestPin);   // raw link - reconstruct-known-topology, no schema side effects
			UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("ImportQuestline: wired [%s] %s(%s) -> [%s] %s(%s)"),
				*E.From,
				*SourcePin->PinName.ToString(),
				*SourcePin->PinType.PinCategory.ToString(),
				*E.To,
				*DestPin->PinName.ToString(),
				*DestPin->PinType.PinCategory.ToString());
		}
	}

		// ---- In-place planning -----------------------------------------------------------------------------------------
	// Compare a validated bundle against an EXISTING asset and describe what a re-import would do, changing nothing.

	// A console-typed asset path is usually the short form "/Game/Path/Asset"; FSoftObjectPath needs "/Game/Path/Asset.Asset".
	FString NormalizeConsoleAssetPath(const FString& In)
	{
		if (In.Contains(TEXT("."))) return In;
		FString Ignored, AssetName;
		if (In.Split(TEXT("/"), &Ignored, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return In + TEXT(".") + AssetName;
		}
		return In;
	}

	// One-line human-readable form of a cell, for the plan's before/after columns.
	FString DescribeValue(const FQuestDataValue& Value)
	{
		switch (Value.Kind)
		{
		case EQuestDataValueKind::Empty:        return TEXT("<default>");
		case EQuestDataValueKind::Tag:          return Value.Tag.IsValid() ? Value.Tag.ToString() : TEXT("<none>");
		case EQuestDataValueKind::TagContainer: return Value.TagContainer.ToStringSimple();
		case EQuestDataValueKind::Text:         return Value.Text.ToString();
		case EQuestDataValueKind::Bool:         return Value.bBool ? TEXT("true") : TEXT("false");
		case EQuestDataValueKind::Array:
		{
			TArray<FString> Parts;
			for (const FQuestDataValue& Element : Value.Elements) Parts.Add(DescribeValue(Element));
			return FString::Printf(TEXT("[%s]"), *FString::Join(Parts, TEXT(", ")));
		}
		default: return Value.StringForm;
		}
	}

	// Re-express an incoming cell in the SAME typed form a live property produces, so the two can be compared meaningfully.
	// A text provider hands us Kind::String for a tag, enum or reference property, while the node's current value builds as
	// Kind::Tag / Kind::Enum / Kind::Reference. Comparing those directly would mark every property changed on every import.
	// Routing the cell through the real restore path onto scratch memory and rebuilding it from the property normalizes both
	// sides through one definition, whatever representation the provider happened to use.
	FQuestDataValue TypeIncomingLikeProperty(const FProperty* Prop, const FQuestDataValue& Cell, const void* SeedPtr)
	{
		void* Scratch = FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment());
		Prop->InitializeValue(Scratch);
		// Start from what the apply step would start from. RestoreCell's non-writing arms leave the seed in place, which is
		// what makes an absent cell mean "unchanged" rather than "reset to zero" - and zero is not even the right default,
		// since a property with an in-class initializer is non-zero on a freshly constructed object.
		// CopyCompleteValue, not CopySingleValue: the buffer is sized at GetSize(), which includes ArrayDim.
		if (SeedPtr) { Prop->CopyCompleteValue(Scratch, SeedPtr); }
		RestoreCell(Prop, Scratch, Cell);
		// Null default: we want the value the source would actually produce, never collapsed to Empty for being at-default.
		FQuestDataValue Typed = BuildQuestDataValue(Prop, Scratch, nullptr);
		Prop->DestroyValue(Scratch);
		FMemory::Free(Scratch);
		return Typed;
	}

	// Compare one object's authored properties against one row's cells. Shared by every object a bundle describes - a node,
	// the questline itself, and (once nested values are described) an instanced child - so the comparison rule has exactly
	// one definition and cannot drift between them. PathPrefix names where this object sits under its owner, empty at the top.
	void DiffObjectAgainstRow(const UObject* Object, const FQuestDataRow& Row, const FString& PathPrefix,
	                          FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver())
	{
		for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
		{
			const FString& Column = Cell.Key;
			if (Column == TEXT("class") || Column == TEXT("graph")) continue;   // structural - describes the row, not a property

			FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*Column));
			if (!Prop)
			{
				// Only a column that actually CARRIES something is worth reporting as unmatched. Columns are declared for a
				// whole table, so an unbound bookkeeping column would otherwise warn once per ROW instead of once.
				if (Cell.Value.Kind != EQuestDataValueKind::Empty)
				{
					OutPlan.Warnings.Add(FString::Printf(TEXT("row '%s' column '%s' matches no property on %s - it would be ignored"),
						*Row.Key,
						*Column,
						*Object->GetClass()->GetName()));
				}
				continue;
			}

			const void* LivePtr = Prop->ContainerPtrToValuePtr<void>(Object);
			
			// An ABSENT cell is the only place policy applies. The source declared the column and left it blank, which is a
			// statement about the default - but which default, and whether blank is permitted at all, is the recipe's call.
			// A cell that CARRIES a value overwrites the seed regardless, so policy never reaches it.
			const bool bAbsent = (Cell.Value.Kind == EQuestDataValueKind::Empty);
			const EQuestAbsentFieldPolicy Policy = bAbsent ? Policies.Resolve(FName(*Column)) : EQuestAbsentFieldPolicy::Preserve;
			if (bAbsent && Policy == EQuestAbsentFieldPolicy::Require)
			{
				OutPlan.Refusals.Add(FString::Printf(TEXT("row '%s' leaves '%s' blank, and the recipe requires a value for it"),
					*Row.Key, *Column));
				continue;
			}

			// THE SEED IS THE POLICY. From the live value, an absent cell changes nothing; from the class default, it resets.
			// The comparison below is byte-for-byte the same either way - only its starting point moves.
			const void* SeedPtr = LivePtr;
			if (bAbsent && Policy == EQuestAbsentFieldPolicy::Reset)
			{
				// The CDO of the class the property was RESOLVED on, not of the incoming class - a property offset is only
				// meaningful against the layout it belongs to.
				const UClass* OwnerClass = Prop->GetOwnerClass();
				if (OwnerClass) { SeedPtr = Prop->ContainerPtrToValuePtr<void>(OwnerClass->GetDefaultObject()); }
			}

			// Compare through the PROPERTY, not the neutral value: FProperty::Identical knows each type's own equality, where
			// a value-level comparison must pick one rule for a Kind that deliberately fuses several types.
			void* Scratch = FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment());
			Prop->InitializeValue(Scratch);
			Prop->CopyCompleteValue(Scratch, SeedPtr);
			RestoreCell(Prop, Scratch, Cell.Value);
			const bool bIdentical = Prop->Identical(Scratch, LivePtr, PPF_None);
			const FQuestDataValue Incoming = BuildQuestDataValue(Prop, Scratch, nullptr);
			Prop->DestroyValue(Scratch);
			FMemory::Free(Scratch);
			if (bIdentical) continue;

			// A reset says WHY the value is what it is, not just what it is. Without that, "the source blanked this field"
			// and "the policy is resetting it to a default that happens to be blank" print identically - and they are
			// different decisions a designer might want to question. The VALUE is untouched; only its description changes.
			const FString Described = DescribeValue(Incoming);
			const bool bResetToDefault = bAbsent && Policy == EQuestAbsentFieldPolicy::Reset;

			FQuestPropertyChange Change;
			Change.Property      = PathPrefix.IsEmpty() ? Column : (PathPrefix + TEXT(".") + Column);
			Change.CurrentText   = DescribeValue(BuildQuestDataValue(Prop, LivePtr, nullptr));
			Change.IncomingText  = !bResetToDefault ? Described
								 : (Described.IsEmpty() ? FString(TEXT("<default>"))
														: FString::Printf(TEXT("<default> (%s)"), *Described));
			Change.IncomingValue = Incoming;   // what apply writes - never re-derived from the row
			Entry.Changes.Add(MoveTemp(Change));
		}
	}
	// True when the bundle describes anything under this property - a row keyed exactly "<owner>/<prop>" (a direct instanced
	// object) or "<owner>/<prop>[..." (a container). This is the SAME rule the restore path applies before it touches a
	// property: a source declaring no children has said nothing about it, so the property is left alone. The plan has to
	// agree, or it describes an apply that will not happen.
	bool BundleDeclaresChildrenUnder(const FQuestDataBundle& Bundle, const FString& PropPrefix)
	{
		const FString Indexed = PropPrefix + TEXT("[");
		for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
		{
			for (const FQuestDataRow& Row : Table.Value.Rows)
			{
				if (Row.Key == PropPrefix || Row.Key.StartsWith(Indexed)) return true;
			}
		}
		return false;
	}

	// Compare an owner's instanced children against the rows describing them. The walk that finds the live children is the
	// SAME one the writer uses to name them, so a row and the object it describes are matched by construction rather than by
	// a second guess at the naming rule - which is exactly where the two directions have drifted before.
	void DiffInstancedChildren(const UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle,
	                           FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver())
	{
		for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			// The same filter that decided what became a child row on the way out.
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)) continue;
			if (!IsQuestInstancedBearing(Prop)) continue;

			const FString PropPrefix = FString::Printf(TEXT("%s/%s"), *OwnerKey, *Prop->GetName());
			if (!BundleDeclaresChildrenUnder(Bundle, PropPrefix)) continue;   // silence is not emptiness

			TSet<FString> LiveKeys;
			ForEachQuestInstancedChild(Prop, Prop->ContainerPtrToValuePtr<void>(Owner), OwnerKey, Prop->GetName(),
				[&](const FString& ChildKey, const FString& Path, const UObject* Child)
				{
					LiveKeys.Add(ChildKey);

					FString ChildClass;
					const FQuestDataRow* ChildRow = FindChildRow(Bundle, ChildKey, ChildClass);
					if (!ChildRow)
					{
						// The source DOES describe this property's contents, and this child is not among them.
						FQuestPropertyChange Change;
						Change.Property     = Path;
						Change.CurrentText  = Child->GetClass()->GetName();
						Change.Kind         = EQuestPropertyChangeKind::ChildRemoved;
						Change.IncomingText = TEXT("<removed>");
						Entry.Changes.Add(MoveTemp(Change));
						return;
					}
					DiffObjectAgainstRow(Child, *ChildRow, Path, Entry, OutPlan, Policies);
					DiffInstancedChildren(Child, ChildKey, Bundle, Entry, OutPlan, Policies);   // a child can itself nest
				});

			// Rows under this property with no live counterpart are additions.
			const FString Indexed = PropPrefix + TEXT("[");
			for (const TPair<FString, FQuestDataTable>& Table : Bundle.TablesByType)
			{
				for (const FQuestDataRow& Row : Table.Value.Rows)
				{
					if (Row.Key != PropPrefix && !Row.Key.StartsWith(Indexed)) continue;
					if (LiveKeys.Contains(Row.Key)) continue;
					// DIRECT children only - a grandchild's key carries a further '/' segment, and belongs to its own owner.
					if (Row.Key.RightChop(OwnerKey.Len() + 1).Contains(TEXT("/"))) continue;

					FQuestPropertyChange Change;
					Change.Property     = Row.Key.RightChop(OwnerKey.Len() + 1);
					Change.Kind         = EQuestPropertyChangeKind::ChildAdded;
					Change.CurrentText  = TEXT("<absent>");
					Change.IncomingText = Row.Get(TEXT("class"));
					Entry.Changes.Add(MoveTemp(Change));
				}
			}
		}
	}
	
	// Every object a change's path could name, keyed by the path that names it: "" for the entity itself, then each instanced
	// descendant under the path the WRITER gives it. Built by the same walk the plan used, so any path the plan produced
	// resolves here by construction. The children are reached from a non-const owner, so widening them back is sound.
	void CollectApplyTargets(UObject* Owner, const FString& OwnerKey, const FString& PathPrefix, TMap<FString, UObject*>& OutByPath)
	{
		OutByPath.Add(PathPrefix, Owner);
		for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)) continue;
			if (!IsQuestInstancedBearing(Prop)) continue;

			ForEachQuestInstancedChild(Prop, Prop->ContainerPtrToValuePtr<void>(Owner), OwnerKey, Prop->GetName(),
				[&OutByPath](const FString& ChildKey, const FString& Path, const UObject* Child)
				{
					CollectApplyTargets(const_cast<UObject*>(Child), ChildKey, Path, OutByPath);
				});
		}
	}

	// Which object owns this change, and under what property name? LONGEST matching path wins, and the candidate must
	// actually have a property by the remaining name - that pair of conditions is what makes this safe without parsing a
	// path whose segments can contain dots and brackets of their own.
	UObject* FindApplyTarget(const TMap<FString, UObject*>& ByPath, const FString& ChangePath, FString& OutPropertyName)
	{
		UObject* Best = nullptr;
		int32 BestLen = -1;
		for (const TPair<FString, UObject*>& Pair : ByPath)
		{
			FString Remainder;
			if (Pair.Key.IsEmpty())                                    { Remainder = ChangePath; }
			else if (ChangePath.StartsWith(Pair.Key + TEXT("."))) { Remainder = ChangePath.RightChop(Pair.Key.Len() + 1); }
			else                                                       { continue; }

			if (Remainder.IsEmpty() || Pair.Key.Len() <= BestLen) continue;
			if (Pair.Value->GetClass()->FindPropertyByName(FName(*Remainder)))
			{
				Best = Pair.Value;
				BestLen = Pair.Key.Len();
				OutPropertyName = Remainder;
			}
		}
		return Best;
	}

	// Properties that carry IDENTITY rather than configuration. QuestlineID is not a field - it is the questline's compiled
	// tag namespace, so rewriting it moves every tag the questline owns, invalidates save data keyed on them, and can
	// collide with another asset. A rename is a deliberate operation with consequences a property write cannot express, so
	// apply reports it and declines. The plan still SHOWS the difference; it simply is not something this step performs.
	bool IsIdentityBearingProperty(const UObject* Target, const FString& PropertyName)
	{
		return Target && Target->IsA<UQuestlineGraph>() && PropertyName == TEXT("QuestlineID");
	}

	int32 ApplyChangesToObject(UObject* Owner, const FString& OwnerKey, const TArray<FQuestPropertyChange>& Changes, TArray<FString>& OutSkipped)
	{
		if (!Owner) return 0;

		TMap<FString, UObject*> ByPath;
		CollectApplyTargets(Owner, OwnerKey, FString(), ByPath);

		int32 Written = 0;
		for (const FQuestPropertyChange& Change : Changes)
		{
			FString PropertyName;
			UObject* Target = FindApplyTarget(ByPath, Change.Property, PropertyName);
			if (!Target)
			{
				// The structure moved between planning and applying, or the plan came from elsewhere. Either way, say so -
				// a change that silently evaporates is worse than one that fails, because the plan already promised it.
				OutSkipped.Add(FString::Printf(TEXT("'%s' names no property reachable from '%s'"), *Change.Property, *OwnerKey));
				continue;
			}
			FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*PropertyName));
			if (!Prop)
			{
				OutSkipped.Add(FString::Printf(TEXT("'%s' resolved to no property"), *Change.Property));
				continue;
			}

			if (IsIdentityBearingProperty(Target, PropertyName))
			{
				OutSkipped.Add(FString::Printf(TEXT("'%s' is identity-bearing - rewriting it would move the questline's compiled "
					"tag namespace and orphan save data keyed on it. Rename deliberately instead."), *Change.Property));
				continue;
			}

			Target->Modify();   // per-object, so undo restores instanced children as well as the node
			// Write the value the PLAN computed, not a re-reading of the row. Once the restore path can decline to write,
			// those are different questions, and answering the second one here is how an apply drifts from its own preview.
			RestoreCell(Prop, Prop->ContainerPtrToValuePtr<void>(Target), Change.IncomingValue);
			++Written;
		}
		return Written;
	}

	// The graph a level names: "root" is the questline's own graph, anything else names a container node whose inner graph
	// it is. Null means the level names nothing reachable - the planner refuses genuinely unreachable levels, so a null here
	// means either the asset moved under us or the container is itself a create that has not spawned yet.
	UEdGraph* ResolveLevelGraph(UQuestlineGraph& Target, const FString& Level, const TMap<FString, UEdGraphNode*>& NodeByKey)
	{
		if (Level.IsEmpty() || Level == TEXT("root")) { return Target.QuestlineEdGraph; }
		if (UEdGraphNode* const* Found = NodeByKey.Find(Level))
		{
			if (UQuestlineNode_Quest* Quest = Cast<UQuestlineNode_Quest>(*Found)) { return Quest->GetInnerGraph(); }
		}
		return nullptr;
	}

	// Execute a plan. Deliberately separate from the console command that currently drives it: the editor action will be a
	// second caller, and a test is a third - and a mutation worth trusting is one that can be exercised without a UI.
	// The caller owns the transaction, because a caller may want several applies inside one undo.
	void ApplyPlan(UQuestlineGraph& Target, const FQuestInPlacePlan& Plan, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, FQuestApplyResult& OutResult, const FQuestApplyOptions& Options)
	{
		// A plan carrying refusals or contested keys is untrustworthy in ANY part - those say the planner could not describe
		// the source, not merely that one row is odd. Acting on the rest would be acting on a description known to be wrong.
		if (!Plan.Refusals.IsEmpty() || !Plan.AmbiguousKeys.IsEmpty())
		{
			OutResult.bRefused = true;
			return;
		}

		// Record the questline for undo WITHOUT dirtying: at this point we do not yet know whether anything will change, and
		// UObject::Modify marks the package dirty by default. The caller dirties once, at the end, only if something did.
		// Every other Modify in this function sits inside a branch that is about to write, so those dirty correctly.
		Target.Modify(false);

		// Existing nodes, addressed the way rows address them AND by GUID, since a source may use either spelling.
		TMap<FString, FString> SourceKeyByGuid;
		TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
		CollectQuestNodeIdentity(Target.QuestlineEdGraph, SourceKeyByGuid, NodeByGuid);
		TMap<FString, UEdGraphNode*> NodeByKey;
		for (const TPair<FString, const UQuestlineNodeBase*>& Pair : NodeByGuid)
		{
			UEdGraphNode* Node = const_cast<UQuestlineNodeBase*>(Pair.Value);
			NodeByKey.Add(QuestNodeIdentityKey(Pair.Key, SourceKeyByGuid), Node);
			NodeByKey.Add(Pair.Key, Node);
		}

		// CREATE before anything else: a container has to exist before nodes inside it, and wiring will reference new nodes.
		{
			TArray<const FQuestNodePlanEntry*> Pending;
			for (const FQuestNodePlanEntry& Entry : Plan.Entries)
			{
				if (Entry.Action == EQuestNodePlanAction::Create) { Pending.Add(&Entry); }
			}

			TSet<FString> Consumed;
			TArray<FString> SpawnWarnings;
			TMap<FString, UEdGraphNode*> CreatedByKey;

			// A source may declare a container AND its contents, so one create's level can be another create. Spawn whatever
			// is reachable and repeat while anything moved; the planner already refuses genuinely unreachable levels, so this
			// settles in a pass or two. Anything still pending is REPORTED rather than dropped - a create that quietly does
			// not happen is the failure mode this whole arc exists to prevent.
			bool bProgress = true;
			while (bProgress && Pending.Num() > 0)
			{
				bProgress = false;
				for (int32 Idx = Pending.Num() - 1; Idx >= 0; --Idx)
				{
					const FQuestNodePlanEntry& Entry = *Pending[Idx];
					UEdGraph* Level = ResolveLevelGraph(Target, Entry.GraphCell, NodeByKey);
					if (!Level) { continue; }

					const FQuestDataRow* const* Row = NodeRowsByKey.Find(Entry.Key);
					if (!Row || !*Row)
					{
						OutResult.Skipped.Add(FString::Printf(TEXT("create '%s' has no row in the source"), *Entry.Key));
					}
					else
					{
						Level->Modify();
						if (UEdGraphNode* Node = SpawnNodeFromRow(Level, **Row, Bundle, NodeByKey, Consumed, SpawnWarnings))
						{
							CreatedByKey.Add(Entry.Key, Node);
							++OutResult.NodesCreated;
						}
						else
						{
							OutResult.Skipped.Add(FString::Printf(TEXT("create '%s' could not be spawned"), *Entry.Key));
						}
					}
					Pending.RemoveAt(Idx);
					bProgress = true;
				}
			}
			for (const FQuestNodePlanEntry* Entry : Pending)
			{
				OutResult.Skipped.Add(FString::Printf(TEXT("create '%s' sits in level '%s', which never became reachable"),
					*Entry->Key, *Entry->GraphCell));
			}

			// Property-derived pins only exist once the properties are restored, and wiring will need them. Restricted to the
			// nodes just created - refreshing pins on nodes nobody asked about would disturb an asset this plan never
			// described.
			if (CreatedByKey.Num() > 0)
			{
				RefreshPinsPass(Bundle, NodeRowsByKey, CreatedByKey, SpawnWarnings);
			}
			OutResult.Skipped.Append(SpawnWarnings);
		}

		// WIRING, after creation so both endpoints of a new relationship exist.
		{
			for (const FQuestDataEdge& Edge : Plan.RemovedEdges)
			{
				UEdGraphNode* const* FromNode = NodeByKey.Find(Edge.From);
				UEdGraphNode* const* ToNode   = NodeByKey.Find(Edge.To);
				if (!FromNode || !ToNode) { OutResult.Skipped.Add(FString::Printf(TEXT("removed edge %s -> %s: endpoint missing"), *Edge.From, *Edge.To)); continue; }

				UEdGraphPin* SourcePin = ResolveSourcePin(*FromNode, Edge.Type);
				UEdGraphPin* DestPin   = SourcePin ? ResolveDestPin(*ToNode, SourcePin->PinType.PinCategory) : nullptr;
				if (!SourcePin || !DestPin) { OutResult.Skipped.Add(FString::Printf(TEXT("removed edge %s -> %s: pin unresolved"), *Edge.From, *Edge.To)); continue; }

				// Break it ONLY when a direct link is what is actually there. The plan's edges are knot-collapsed, so this
				// relationship may run through reroute knots that fan out to other targets as well - breaking the first hop
				// would remove those relationships too, and deleting the chain would destroy routing placed deliberately.
				// Neither is derivable from the data, so a knot-routed removal is reported instead of guessed at.
				if (!SourcePin->LinkedTo.Contains(DestPin))
				{
					OutResult.Skipped.Add(FString::Printf(TEXT("removed edge %s %s %s is routed through reroute nodes - "
						"rewire it by hand, or the knots' other targets would go with it"), *Edge.From, *Edge.Type, *Edge.To));
					continue;
				}
				(*FromNode)->Modify();
				(*ToNode)->Modify();
				SourcePin->BreakLinkTo(DestPin);
				++OutResult.EdgesChanged;
			}

			for (const FQuestDataEdge& Edge : Plan.AddedEdges)
			{
				UEdGraphNode* const* FromNode = NodeByKey.Find(Edge.From);
				UEdGraphNode* const* ToNode   = NodeByKey.Find(Edge.To);
				if (!FromNode || !ToNode) { OutResult.Skipped.Add(FString::Printf(TEXT("added edge %s -> %s: endpoint missing"), *Edge.From, *Edge.To)); continue; }

				UEdGraphPin* SourcePin = ResolveSourcePin(*FromNode, Edge.Type);
				UEdGraphPin* DestPin   = SourcePin ? ResolveDestPin(*ToNode, SourcePin->PinType.PinCategory) : nullptr;
				if (!SourcePin || !DestPin) { OutResult.Skipped.Add(FString::Printf(TEXT("added edge %s -> %s: pin unresolved"), *Edge.From, *Edge.To)); continue; }
				if (SourcePin->LinkedTo.Contains(DestPin)) { continue; }   // already there; the comparison saw it through knots

				(*FromNode)->Modify();
				(*ToNode)->Modify();
				SourcePin->MakeLinkTo(DestPin);
				++OutResult.EdgesChanged;
			}
		}

		// DELETION LAST, and only when explicitly permitted. This is the one operation here that destroys authored work, so
		// it is opt-in twice: the plan reports an orphan whatever the setting, and removal happens only if asked. The plan has
		// already scoped orphans to levels the source DECLARES - a source describing one container says nothing about the
		// rest of the asset - and this honours that scoping rather than re-deriving it, which is where it would drift.
		if (Options.bDeleteOrphanedNodes)
		{
			for (const FQuestNodePlanEntry& Entry : Plan.Entries)
			{
				if (Entry.Action != EQuestNodePlanAction::Orphan) { continue; }

				UEdGraphNode* Node = NodeByKey.FindRef(Entry.Guid);
				if (!Node) { OutResult.Skipped.Add(FString::Printf(TEXT("orphan '%s' no longer resolves to a node"), *Entry.Key)); continue; }

				// A container carries its inner graph, so removing one takes everything inside it - nodes the plan counted as
				// UNTOUCHED rather than as orphans. Deleting a container by hand behaves the same way, so this is not refused,
				// but the blast radius has to be stated: a destructive step that under-reports what it removed is worse than
				// one that refuses.
				if (const UQuestlineNode_Quest* Quest = Cast<UQuestlineNode_Quest>(Node))
				{
					if (const UEdGraph* Inner = Quest->GetInnerGraph())
					{
						if (Inner->Nodes.Num() > 0)
						{
							OutResult.Skipped.Add(FString::Printf(TEXT("deleting container '%s' also removed %d node(s) inside it"),
								*Entry.Key, Inner->Nodes.Num()));
						}
					}
				}

				UEdGraph* OwningGraph = Node->GetGraph();
				if (!OwningGraph) { OutResult.Skipped.Add(FString::Printf(TEXT("orphan '%s' has no owning graph"), *Entry.Key)); continue; }

				OwningGraph->Modify();
				Node->Modify();
				Node->BreakAllNodeLinks();
				OwningGraph->RemoveNode(Node);
				++OutResult.NodesDeleted;
			}
		}

		for (const FQuestNodePlanEntry& Entry : Plan.Entries)
		{
			if (Entry.Action == EQuestNodePlanAction::Create) { continue; }   // already handled by the creation pass above
			if (Entry.Action == EQuestNodePlanAction::Orphan)
			{
				// Deferred only when deletion is not permitted. When it is, the deletion pass owns these - counting them here
				// as well would report the same node as both removed and left alone.
				if (!Options.bDeleteOrphanedNodes) { ++OutResult.EntriesDeferred; }
				continue;
			}
			if (Entry.Action != EQuestNodePlanAction::Update) { ++OutResult.EntriesDeferred; continue; }
			if (Entry.bStructuralChange)
			{
				// A class change or a move between levels cannot be edited into place, and doing the property half would
				// leave a node matching neither its source nor its former self.
				++OutResult.EntriesDeferred;
				continue;
			}
			if (Entry.Changes.IsEmpty()) continue;

			UObject* Object = Entry.bIsQuestlineSelf ? static_cast<UObject*>(&Target) : NodeByKey.FindRef(Entry.Guid);
			if (!Object)
			{
				OutResult.Skipped.Add(FString::Printf(TEXT("entry '%s' no longer resolves to an object"), *Entry.Key));
				continue;
			}
			OutResult.PropertiesWritten += ApplyChangesToObject(Object, Entry.Key, Entry.Changes, OutResult.Skipped);
		}
	}

	void PlanInPlace(const UQuestlineGraph& Target, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, const TArray<FString>& ReadWarnings, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver())
	{
		OutPlan.Warnings = ReadWarnings;

		// The questline's own authored properties. A re-import writes these exactly as it writes a node's, so a plan that
		// describes only nodes would report an unchanged questline while a rename was pending.
		if (const FQuestDataTable* SelfTable = Bundle.TablesByType.Find(TEXT("questline_graph")))
		{
			if (SelfTable->Rows.Num() > 0)
			{
				FQuestNodePlanEntry SelfEntry;
				SelfEntry.Action           = EQuestNodePlanAction::Update;
				SelfEntry.bIsQuestlineSelf = true;
				SelfEntry.Key              = SelfTable->Rows[0].Key;
				SelfEntry.ClassName        = Target.GetClass()->GetName();
				SelfEntry.CurrentClassName = SelfEntry.ClassName;

				// A FABRICATED self row asserts nothing. The DataTable provenance has no self row, so one is invented from the
				// table's asset name purely to satisfy validation - comparing it would let a source that never described the
				// questline rename it. Entered anyway, so the plan still accounts for the questline, but with no changes.
				if (!Bundle.bSelfRowSynthesized)
				{
					DiffObjectAgainstRow(&Target, SelfTable->Rows[0], FString(), SelfEntry, OutPlan, Policies);
					DiffInstancedChildren(&Target, SelfTable->Rows[0].Key, Bundle, SelfEntry, OutPlan, Policies);
				}
				SelfEntry.Label = Target.GetName();
				OutPlan.Entries.Add(MoveTemp(SelfEntry));
			}
		}

		// Read the existing nodes under the SAME keying rule the spawn path writes: a studio-authored key when the node
		// carries one, else our GUID. That is the dual-key identity contract read in reverse.
		TMap<FString, FString> SourceKeyByGuid;
		TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
		TMap<FString, FString> GraphCellByGuid;
		CollectQuestNodeIdentity(Target.QuestlineEdGraph, SourceKeyByGuid, NodeByGuid, &GraphCellByGuid);

		TArray<FString> AllGuids;
		NodeByGuid.GetKeys(AllGuids);
		TMap<FString, FString> GuidByKey;
		BuildQuestNodeKeyIndex(SourceKeyByGuid, AllGuids, GuidByKey, OutPlan.AmbiguousKeys);
		for (const FString& Key : OutPlan.AmbiguousKeys)
		{
			OutPlan.Warnings.Add(FString::Printf(TEXT("'%s' names more than one node - that row is not planned, and neither "
				"claimant is treated as an orphan. Clear the import provenance on the duplicate to resolve it."), *Key));
		}

		// The levels this source is talking about, in the one namespace. Anything outside them is none of its business.
		TSet<FString> DeclaredLevels;
		for (const TPair<FString, const FQuestDataRow*>& Pair : NodeRowsByKey)
		{
			DeclaredLevels.Add(ResolveQuestLevelToGuid(Pair.Value->Get(TEXT("graph")), GuidByKey));
		}

		TSet<FString> MatchedGuids;
		for (const TPair<FString, const FQuestDataRow*>& Pair : NodeRowsByKey)
		{
			const FQuestDataRow& Row = *Pair.Value;

			FQuestNodePlanEntry Entry;
			Entry.Key       = Row.Key;
			Entry.ClassName = Row.Get(TEXT("class"));
			Entry.GraphCell = Row.Get(TEXT("graph"));

			const FString* FoundGuid = GuidByKey.Find(Row.Key);
			const UQuestlineNodeBase* Node = FoundGuid ? NodeByGuid.FindRef(*FoundGuid) : nullptr;
			if (!Node)
			{
				// Only promise what the spawn path can deliver. A class no loaded module provides, or a level nothing declares,
				// both make SpawnNodeFromRow / ImportGraphLevel skip the row - so planning a CREATE would report work that
				// silently never happens, which is worse than reporting nothing.
				if (!ResolveBundleClass(Entry.ClassName))
				{
					OutPlan.Refusals.Add(FString::Printf(TEXT("row '%s' names class '%s', which no loaded module provides - "
						"it cannot be created"), *Row.Key, *Entry.ClassName));
					continue;
				}
				const FString Level = ResolveQuestLevelToGuid(Entry.GraphCell, GuidByKey);
				const bool bLevelExists = Level.IsEmpty() || Level == TEXT("root")
					|| GuidByKey.Contains(Level) || NodeRowsByKey.Contains(Entry.GraphCell);
				if (!bLevelExists)
				{
					OutPlan.Refusals.Add(FString::Printf(TEXT("row '%s' sits in level '%s', which no node or row declares - "
						"it cannot be reached"), *Row.Key, *Entry.GraphCell));
					continue;
				}

				Entry.Action = EQuestNodePlanAction::Create;
				// A create has nothing to ask, so this is whatever the source volunteered - often nothing, which is honest.
				Entry.Label = Row.Get(TEXT("NodeLabel"));
				OutPlan.Entries.Add(MoveTemp(Entry));
				continue;
			}

			MatchedGuids.Add(*FoundGuid);
			Entry.Action           = EQuestNodePlanAction::Update;
			Entry.Guid             = *FoundGuid;
			Entry.CurrentClassName = Node->GetClass()->GetName();
			Entry.CurrentGraphCell = GraphCellByGuid.FindRef(*FoundGuid);

			// Compare levels in ONE namespace. The two sides spell a level differently by design - the writer names it by the
			// container's GUID, the asset walk by whatever key that container answers to - so a raw compare would flag every
			// node inside a semantically-keyed container as needing a rebuild it does not need.
			const FString IncomingLevel = ResolveQuestLevelToGuid(Entry.GraphCell, GuidByKey);
			const FString CurrentLevel  = ResolveQuestLevelToGuid(Entry.CurrentGraphCell, GuidByKey);
			Entry.bStructuralChange = (Entry.ClassName != Entry.CurrentClassName) || (IncomingLevel != CurrentLevel);

			DiffObjectAgainstRow(Node, Row, FString(), Entry, OutPlan, Policies);
			DiffInstancedChildren(Node, Row.Key, Bundle, Entry, OutPlan, Policies);

			// FullTitle, not ListView: ListView is a palette context, and a node that distinguishes them answers it with its
			// TYPE label - which would give every placement of that class the same name to sort and display by.
			Entry.Label = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			OutPlan.Entries.Add(MoveTemp(Entry));
		}

		// Anything the asset holds that the source no longer mentions - but ONLY within the levels the source actually
		// declares. A source describing one container says nothing about the rest of the asset, and treating its silence as
		// deletion is what would make a delete-orphans policy unsafe to offer at all.
		for (const TPair<FString, const UQuestlineNodeBase*>& Pair : NodeByGuid)
		{
			if (MatchedGuids.Contains(Pair.Key)) continue;

			const FString NodeLevel = ResolveQuestLevelToGuid(GraphCellByGuid.FindRef(Pair.Key), GuidByKey);
			if (!DeclaredLevels.Contains(NodeLevel)) { ++OutPlan.UntouchedNodeCount; continue; }

			// A contested key names two nodes; neither is a row's target, and neither may be deleted on that basis.
			if (OutPlan.AmbiguousKeys.Contains(QuestNodeIdentityKey(Pair.Key, SourceKeyByGuid))) continue;

			FQuestNodePlanEntry Entry;
			Entry.Action           = EQuestNodePlanAction::Orphan;
			Entry.Key              = QuestNodeIdentityKey(Pair.Key, SourceKeyByGuid);
			Entry.Guid             = Pair.Key;
			Entry.ClassName        = Pair.Value->GetClass()->GetName();
			Entry.CurrentClassName = Entry.ClassName;
			Entry.CurrentGraphCell = GraphCellByGuid.FindRef(Pair.Key);
			Entry.Label            = Pair.Value->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			OutPlan.Entries.Add(MoveTemp(Entry));
		}

		// Wiring. Derived from the live graph with the SAME walk the writer uses, so an unchanged questline compares clean
		// rather than looking rewired. Without this a source that only re-points a "next" column reports no change at all,
		// and a synthesized gate appears as a CREATE while the edges that make it function stay invisible.
		{
			const TUniquePtr<FQuestlineGraphTraversalPolicy> Policy = MakeUnique<FQuestlineGraphTraversalPolicy>();
			TArray<FQuestDataEdge> LiveEdges;
			for (const TPair<FString, const UQuestlineNodeBase*>& Pair : NodeByGuid)
			{
				CollectQuestWireEdges(Pair.Value, *Policy, LiveEdges);
			}
			CompareQuestEdges(Bundle.Edges, LiveEdges, GuidByKey, OutPlan.AddedEdges, OutPlan.RemovedEdges);
		}
		
		/**
		 * Deterministic order, because everything above walks TMaps and two identical plans would otherwise render differently
		 * - which makes a panel feel broken and makes two screenshots impossible to compare. The questline itself leads, then
		 * by action so like sits with like, then by name. A panel re-sorts on top of this; it only needs a stable start.
		 */
		OutPlan.Entries.Sort([](const FQuestNodePlanEntry& A, const FQuestNodePlanEntry& B)
		{
			if (A.bIsQuestlineSelf != B.bIsQuestlineSelf) { return A.bIsQuestlineSelf; }
			if (A.Action != B.Action) { return static_cast<uint8>(A.Action) < static_cast<uint8>(B.Action); }
			const FString& AName = A.Label.IsEmpty() ? A.Key : A.Label;
			const FString& BName = B.Label.IsEmpty() ? B.Key : B.Label;
			// Natural order, so "Chapter 10" follows "Chapter 9" instead of "Chapter 1". Authored labels routinely end in a
			// number, and that is exactly the case a plain lexicographic sort scatters worst.
			const int32 ByName = UE::ComparisonUtility::CompareNaturalOrder(AName, BName);
			return ByName == 0 ? A.Key < B.Key : ByName < 0;   // key breaks ties, so duplicates never swap
		});
		for (FQuestNodePlanEntry& Entry : OutPlan.Entries)
		{
			Entry.Changes.Sort([](const FQuestPropertyChange& A, const FQuestPropertyChange& B) { return A.Property < B.Property; });
		}
	}

	const TCHAR* PlanActionName(EQuestNodePlanAction Action)
	{
		switch (Action)
		{
		case EQuestNodePlanAction::Create: return TEXT("CREATE");
		case EQuestNodePlanAction::Orphan: return TEXT("ORPHAN");
		default:                           return TEXT("UPDATE");
		}
	}

	void LogInPlacePlan(const FQuestInPlacePlan& Plan)
	{
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: in-place PLAN for '%s' - %d update(s) (%d with changes), %d create(s), %d orphan(s), "
			"%d node(s) outside the levels this source declares, %d contested key(s), %d wire edge(s) added, %d removed. Nothing was modified."),
			*Plan.TargetAssetPath,
			Plan.CountOf(EQuestNodePlanAction::Update),
			Plan.ChangedNodeCount(),
			Plan.CountOf(EQuestNodePlanAction::Create),
			Plan.CountOf(EQuestNodePlanAction::Orphan),
			Plan.UntouchedNodeCount,
			Plan.AmbiguousKeys.Num(),
			Plan.AddedEdges.Num(),
			Plan.RemovedEdges.Num());

		for (const FQuestDataEdge& E : Plan.RemovedEdges) { UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [WIRE-] %s|%s|%s"), *E.From, *E.Type, *E.To); }
		for (const FQuestDataEdge& E : Plan.AddedEdges)   { UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [WIRE+] %s|%s|%s"), *E.From, *E.Type, *E.To); }
		for (const FString& R : Plan.Refusals) { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("  [REFUSED] %s"), *R); }

		for (const FQuestNodePlanEntry& Entry : Plan.Entries)
		{
			// Unchanged matches are the common case on a healthy re-import; listing them would bury the ones that matter.
			if (Entry.Action == EQuestNodePlanAction::Update && Entry.Changes.Num() == 0 && !Entry.bStructuralChange) continue;

			// An orphan has no incoming row, so its level is only known from the asset side; the questline itself sits in no
			// level at all, being the thing levels belong to.
			const FString& Level = (Entry.Action == EQuestNodePlanAction::Orphan) ? Entry.CurrentGraphCell : Entry.GraphCell;
			const FString Where = Entry.bIsQuestlineSelf ? FString(TEXT("the questline itself")) : FString::Printf(TEXT("graph '%s'"), *Level);
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [%s] %s (%s) - %s%s"),
				PlanActionName(Entry.Action),
				*Entry.Key,
				*Entry.ClassName,
				*Where,
				Entry.bStructuralChange ? TEXT("  ** structural: needs rebuild, not an edit **") : TEXT(""));

			if (Entry.bStructuralChange && Entry.ClassName != Entry.CurrentClassName)
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("      class: %s -> %s"), *Entry.CurrentClassName, *Entry.ClassName);
			}
			if (Entry.bStructuralChange && Entry.GraphCell != Entry.CurrentGraphCell)
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("      graph: %s -> %s"), *Entry.CurrentGraphCell, *Entry.GraphCell);
			}
			for (const FQuestPropertyChange& Change : Entry.Changes)
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("      %s: '%s' -> '%s'"), *Change.Property, *Change.CurrentText, *Change.IncomingText);
			}
		}

		for (const FString& W : Plan.Warnings) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: %s"), *W);
		if (Plan.IsNoOp())
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: the asset already matches the source - a re-import would change nothing."));
		}
	}

	void ImportQuestlineCmd(const TArray<FString>& Args)
	{
		// Separate the optional "--format=<name>" arg from the positional path args BEFORE rejoining (it must not get
		// swept into the space-rejoined folder path). PathArgs = every arg that isn't a --flag.
		// --datatable=<AssetPath> selects the asset provenance; then no source FOLDER is needed (the dest path is the only
		// positional arg). Otherwise the folder is the positional args before the dest, space-rejoined.
		FString DataTablePath;
		FString InPlacePath;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--datatable="))) DataTablePath = Arg.RightChop(12);
			else if (Arg.StartsWith(TEXT("--in-place="))) InPlacePath = Arg.RightChop(11);
		}
		const bool bInPlace = !InPlacePath.IsEmpty();
		const bool bApply = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--apply"), ESearchCase::IgnoreCase); });
		const bool bResetAbsent = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--reset-absent"), ESearchCase::IgnoreCase); });
		const bool bDeleteOrphans = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--delete-orphans"), ESearchCase::IgnoreCase); });

		TArray<FString> PathArgs;
		for (const FString& Arg : Args)
		{
			if (!Arg.StartsWith(TEXT("--"))) PathArgs.Add(Arg);
		}
		// A source folder unless the DataTable provenance supplies it, plus a dest package unless --in-place names the target.
		const int32 MinPositional = (DataTablePath.IsEmpty() ? 1 : 0) + (bInPlace ? 0 : 1);
		if (PathArgs.Num() < MinPositional)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: usage 'SimpleQuest.ImportQuestline <FolderPath> <DestPackagePath> [--format=json] [--mapping=<asset>]' "
				"or 'SimpleQuest.ImportQuestline <DestPackagePath> --datatable=<asset> [--mapping=<asset>]'. "
				"Add '--in-place=<AssetPath>' to compare against an existing asset instead of creating one; the dest package arg is then omitted."));
			return;
		}

		// --in-place takes no dest package arg, but the create form does - so adapting one command into the other easily
		// leaves the dest behind. Space-rejoining would swallow it into the folder path, and the only symptom would be a
		// "folder not found" naming a path the caller never typed. Name the actual mistake instead. Guarded on there being
		// more than one positional arg so a genuine root-anchored source folder is never mistaken for a package path.
		if (bInPlace && PathArgs.Num() > 1 && FPackageName::IsValidLongPackageName(PathArgs.Last()))
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: trailing argument '%s' is a package path, which --in-place does not take - the "
				"target is named by --in-place=<AssetPath>. Pass only the source folder. Nothing was modified."), *PathArgs.Last());
			return;
		}

		// Console arg tokenization splits on whitespace and does NOT honor quotes, so a folder path containing spaces
		// (e.g. "E:/Unreal Projects/...") arrives as multiple Args. The dest package path is the LAST path arg (never has
		// spaces - it's a /Game/... mount path); the folder path is everything before it, rejoined with spaces. With
		// --in-place there is no dest arg to peel off, so the whole positional remainder is the folder.
		const FString DestPackagePath = bInPlace ? FString() : PathArgs.Last();
		FString FolderPath;
		if (DataTablePath.IsEmpty())
		{
			TArray<FString> FolderParts = PathArgs;
			if (!bInPlace) FolderParts.Pop();                 // drop the dest path
			FolderPath = FString::Join(FolderParts, TEXT(" "));
			FolderPath = FolderPath.TrimQuotes();             // tolerate quotes if the caller added them
		}

		// The source is an ENDPOINT: a file folder read through a format provider, or a DataTable asset. One read call, so
		// the import path never branches on provenance again.
		FQuestDataEndpoint Endpoint;
		if (!DataTablePath.IsEmpty())
		{
			// A console-typed asset path is usually the short form; normalize so --datatable accepts the same form --mapping does.
			Endpoint.Kind = EQuestEndpointKind::DataTable;
			Endpoint.Table = TSoftObjectPtr<UDataTable>(FSoftObjectPath(NormalizeConsoleAssetPath(DataTablePath)));
		}
		else
		{
			const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args, TEXT("ImportQuestline"));
			if (!Format)
			{
				return;   // the unregistered-format error was already logged; refuse before creating anything.
			}
			Endpoint.Kind = EQuestEndpointKind::ForeignFile;
			Endpoint.FormatName = Format->FormatName();
			Endpoint.Folder = FolderPath;
		}

		FQuestDataBundle Bundle;
		FString ReadError;
		if (!ReadEndpointBundle(Endpoint, Bundle, ReadError))
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: %s. No asset created."), *ReadError);
			return;
		}

		TArray<FString> Warnings;

		// A studio's source-shape translation (optional). Absent = the source is already in our shape (our own
		// round-trip / export-as-teacher), so no mapping is needed. Runs before flow-conventions so a mapped column can
		// feed a convention (e.g. a source column mapped onto unlock_after).
		if (const UQuestImportMapping* Mapping = LoadQuestMappingArg(Args))
		{
			if (!ApplyMapping(Bundle, *Mapping, Warnings))
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: mapping guard refused the import. No asset created."));
				return;
			}
			// After the column renames, before the conventions: a wire column is studio vocabulary, never a property.
			ApplyWireBindings(Bundle, *Mapping, Warnings);
		}
		ApplyFlowConventions(Bundle, Warnings);

		TMap<FString, const FQuestDataRow*> NodeRowsByKey;
		TSet<FString> AllRowKeys;
		FString Error;
		if (!ValidateBundle(Bundle, NodeRowsByKey, AllRowKeys, Error))
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: validation failed - %s. No asset created."), *Error);
			return;
		}

		// In-place: describe what a re-import WOULD do to the existing asset, then stop. Planning is read-only and is the
		// only thing --in-place does; applying a plan is a separate, explicitly-requested step. That ordering means a
		// mistyped target path can never damage an asset. The plan is produced as data rather than only logged, so the
		// editor action can render exactly what the console prints.
		if (bInPlace)
		{
			const FString AssetPath = NormalizeConsoleAssetPath(InPlacePath);
			UQuestlineGraph* TargetGraph = Cast<UQuestlineGraph>(FSoftObjectPath(AssetPath).TryLoad());
			if (!TargetGraph || !TargetGraph->QuestlineEdGraph)
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: --in-place target '%s' did not load as a questline graph. Nothing was modified."), *AssetPath);
				return;
			}

			FQuestInPlacePlan Plan;
			Plan.TargetAssetPath = AssetPath;

			// Policy comes from the recipe when there is one, and from the flag otherwise. Preserve stays the default in both
			// cases: the destructive reading should never be what you get by typing nothing.
			FQuestAbsentPolicyResolver Policies;
			if (const UQuestImportMapping* Mapping = LoadQuestMappingArg(Args))
			{
				Policies.Default = Mapping->DefaultAbsentPolicy;
				for (const FQuestColumnBinding& B : Mapping->Bindings)
				{
					if (!B.TargetProperty.IsNone()) { Policies.ByProperty.Add(B.TargetProperty, B.AbsentPolicy); }
				}
			}

			FQuestApplyOptions ApplyOptions;
			if (const UQuestImportMapping* Mapping = LoadQuestMappingArg(Args))
			{
				ApplyOptions.bDeleteOrphanedNodes = Mapping->bDeleteOrphanedNodes;
			}
			if (bDeleteOrphans) { ApplyOptions.bDeleteOrphanedNodes = true; }
			
			// The flag is a RUN-LEVEL instruction and has to reach the per-property entries, not just the fallback: loading a
			// recipe writes an explicit entry for every bound column, and a per-binding Preserve would otherwise shadow the
			// flag for exactly the columns the recipe covers. It says "do not preserve", so it upgrades Preserve to Reset and
			// leaves Require alone - Require is a designer asserting a value must be present, which a convenience flag has no
			// business switching off.
			if (bResetAbsent)
			{
				if (Policies.Default == EQuestAbsentFieldPolicy::Preserve)
				{
					Policies.Default = EQuestAbsentFieldPolicy::Reset;
				}
				for (TPair<FName, EQuestAbsentFieldPolicy>& Entry : Policies.ByProperty)
				{
					if (Entry.Value == EQuestAbsentFieldPolicy::Preserve) { Entry.Value = EQuestAbsentFieldPolicy::Reset; }
				}
			}

			// Say what mode this run is in. A plan is only interpretable against the policy that produced it - the same source
			// and asset yield different plans under Preserve and Reset - and the console does not reliably echo the command
			// that produced a log, so the output has to identify itself.
			const TCHAR* PolicyName =
				Policies.Default == EQuestAbsentFieldPolicy::Reset   ? TEXT("Reset") :
				Policies.Default == EQuestAbsentFieldPolicy::Require ? TEXT("Require") : TEXT("Preserve");
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: in-place %s - source '%s', absent-field policy %s%s.%s"),
				bApply ? TEXT("APPLY") : TEXT("PLAN (read-only)"),
				DataTablePath.IsEmpty() ? *FolderPath : *DataTablePath,
				PolicyName,
				bResetAbsent ? TEXT(" (via --reset-absent)") : TEXT(""),
				bApply && ApplyOptions.bDeleteOrphanedNodes ? TEXT(" [WILL DELETE ORPHANS]") : TEXT(""));
			
			PlanInPlace(*TargetGraph, Bundle, NodeRowsByKey, Warnings, Plan, Policies);
			LogInPlacePlan(Plan);

			if (!bApply)
			{
				return;   // planning is the default; mutating is opted into
			}

			// A plan carrying refusals or contested keys is not trustworthy in ANY part - those say the planner could not
			// describe the source, not merely that one row is odd. Applying the rest would be acting on a description we
			// already know is incomplete.
			if (!Plan.Refusals.IsEmpty() || !Plan.AmbiguousKeys.IsEmpty())
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: --apply refused - the plan carries %d refusal(s) and %d contested key(s). "
					"Resolve those and re-plan. Nothing was modified."),
					Plan.Refusals.Num(),
					Plan.AmbiguousKeys.Num());
				return;
			}

			const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "ApplyInPlaceImport", "Apply In-Place Import"));

			FQuestApplyResult Result;
			ApplyPlan(*TargetGraph, Plan, Bundle, NodeRowsByKey, Result, ApplyOptions);
			if (Result.bRefused)
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: --apply refused - the plan carries %d refusal(s) and %d contested key(s). "
					"Resolve those and re-plan. Nothing was modified."), Plan.Refusals.Num(), Plan.AmbiguousKeys.Num());
				return;
			}

			for (const FString& S : Result.Skipped) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: apply skipped %s"), *S);
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: APPLIED to '%s' - %d property change(s), %d node(s) created, "
				"%d wire edge(s) changed, %d node(s) DELETED. %d entry/entries deferred (structural rebuilds are not "
				"performed). %d skipped."),
				*AssetPath,
				Result.PropertiesWritten,
				Result.NodesCreated,
				Result.EdgesChanged,
				Result.NodesDeleted,
				Result.EntriesDeferred,
				Result.Skipped.Num());

			// Only dirty the package if something actually happened. A re-import that changes nothing should leave no trace:
			// marking it regardless makes every no-op run look like a modification, which costs a save and a diff for work
			// that was not done - and trains a designer to ignore the one signal that says an asset moved.
			const bool bChangedAnything = Result.PropertiesWritten > 0 || Result.NodesCreated > 0 || Result.EdgesChanged > 0 || Result.NodesDeleted > 0;
			if (bChangedAnything)
			{
				TargetGraph->GetPackage()->MarkPackageDirty();
			}
			else
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: nothing to apply - the asset already matches the source. "
					"Package left clean."));
			}
			return;
		}

		// P1 - create the asset via the factory, then restore the self row (with _RT identity + instanced rewards).
		// Two distinct identities: the ROW KEY (sanitized EffectiveID - folder name, tag namespace) and the authored
		// QuestlineID FIELD (raw, whatever the designer typed, spaces and all - the compiler sanitizes it only when
		// building tags, never mutating the field). The asset NAME rides the sanitized key (a package name can't hold
		// spaces); the QuestlineID FIELD must preserve the raw authored value so the round-trip doesn't alter it.
		const FQuestDataRow& SelfRow = Bundle.TablesByType[TEXT("questline_graph")].Rows[0];
		const FString OriginalKey = SelfRow.Key;                          // sanitized - folder/tag identity
		const FString RawQuestlineID = SelfRow.Get(TEXT("QuestlineID"));  // raw authored field (may be empty)
		const FString AssetName = OriginalKey + TEXT("_RT");              // _RT so the compiled tag namespace doesn't collide.

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UQuestlineGraphFactory* Factory = NewObject<UQuestlineGraphFactory>();
		UObject* Created = AssetTools.CreateAsset(AssetName, DestPackagePath, UQuestlineGraph::StaticClass(), Factory);
		UQuestlineGraph* Graph = Cast<UQuestlineGraph>(Created);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: asset creation failed at '%s/%s'."), *DestPackagePath, *AssetName);
			return;
		}
		
		TSet<FString> Consumed;

		// Self-row properties onto the graph object (QuestlineID gets _RT; instanced QuestlineRewards rebuilt).
		RestoreRowProperties(Graph, SelfRow);
		{
			// QuestlineID handling for the round-trip. Two cases, because GetEffectiveID() falls back to the ASSET
			// NAME when the field is empty:
			//   - Source field NON-empty: set the RT field to <raw>_RT, so re-export's QuestlineID cell matches the
			//     source's modulo _RT.
			//   - Source field EMPTY (asset-name-derived): LEAVE IT EMPTY. The source's EffectiveID was its asset
			//     name (e.g. "QL_Ch5_Blocking"); the RT asset's name is "<name>_RT", so the same empty->asset-name
			//     fallback yields "<name>_RT" - matching the source modulo _RT. Writing the literal "_RT" here (the
			//     prior bug) would make QuestlineID = "_RT", tags = SimpleQuest.Questline._RT.*, and the export folder
			//     "_RT" - diverging from the asset-name identity the source actually used.
			if (!RawQuestlineID.IsEmpty())
			{
				if (FProperty* IDProp = Graph->GetClass()->FindPropertyByName(TEXT("QuestlineID")))
				{
					const FString RT = RawQuestlineID + TEXT("_RT");
					IDProp->ImportText_Direct(*RT, IDProp->ContainerPtrToValuePtr<void>(Graph), nullptr, PPF_None);
				}
			}
			// else: RestoreRowProperties already left it empty (the source cell was empty) - nothing to do.
		}
		ReattachInstanced(Graph, OriginalKey, Bundle, Consumed, Warnings);   // self-row child keys are prefixed by the self key

		// P2 - spawn nodes, root graph first, recursing into container inner graphs.
		TMap<FString, UEdGraphNode*> NodeByKey;
		ImportGraphLevel(Graph->QuestlineEdGraph, TEXT("root"), Bundle, NodeRowsByKey, NodeByKey, Consumed, Warnings);

		// P3 - pin refresh pass (innermost-first).
		RefreshPinsPass(Bundle, NodeRowsByKey, NodeByKey, Warnings);

		// P4 - wire edges + contains-edge cross-check.
		WireEdges(Bundle, NodeByKey, Consumed, Warnings);

		// Wrap the double-compile in a compile batch so the gameplay-tag-tree rebuild coalesces to ONCE (at EndCompileBatch)
		// instead of once PER compile pass. The batch's incremental AddNativeTagsForGraph keeps pass 2's RequestGameplayTag
		// lookups valid against pass 1's registrations (that's exactly what the first-compile-identity double-compile needs),
		// while the expensive tree rebuild + INI write defer to batch end. Same mechanism CompileAllQuestlineGraphs uses.
		ISimpleQuestEditorModule::Get().BeginCompileBatch();
		TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
		Compiler->Compile(Graph);                          // pass 1: registers the identity + state tags
		const bool bCompiled = Compiler->Compile(Graph);   // pass 2: identity now valid -> complete resolution records
		ISimpleQuestEditorModule::Get().EndCompileBatch();

		UPackage* Package = Graph->GetPackage();
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Graph);
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Graph, *FileName, SaveArgs);

		for (const FString& W : Warnings) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: %s"), *W);
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: '%s' -> '%s/%s' - %d node(s), %d edge(s), %d warning(s), compile %s. Run C (re-export + diff) and B2 (DumpCompiled + diff) to verify."),
			*OriginalKey,
			*DestPackagePath,
			*AssetName,
			NodeByKey.Num(),
			Bundle.Edges.Num(),
			Warnings.Num(),
			bCompiled ? TEXT("OK") : TEXT("FAILED"));
	}
}

// ---- Transform seam --------------------------------------------------------------------------------------------
// External linkage for the transforms above, without moving them away from the routing code they belong with.
// See Resolver/QuestBundleTransforms.h.

bool QuestBundle_ApplyMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
{
	return ApplyMapping(Bundle, Mapping, Warnings);
}

void QuestBundle_ApplyWireBindings(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
{
	ApplyWireBindings(Bundle, Mapping, Warnings);
}

void QuestBundle_RestoreCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value)
{
	RestoreCell(Prop, ValuePtr, Value);
}

void QuestBundle_ReattachInstanced(UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle, TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
{
	ReattachInstanced(Owner, OwnerKey, Bundle, OutConsumed, OutWarnings);
}

void QuestBundle_ApplyFlowConventions(FQuestDataBundle& Bundle, TArray<FString>& Warnings)
{
	ApplyFlowConventions(Bundle, Warnings);
}

FQuestDataValue QuestBundle_TypeIncomingLikeProperty(const FProperty* Prop, const FQuestDataValue& Cell, const void* SeedPtr)
{
	return TypeIncomingLikeProperty(Prop, Cell, SeedPtr);
}

void QuestBundle_PlanInPlace(const UQuestlineGraph& Target, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, const TArray<FString>& ReadWarnings, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies)
{
	PlanInPlace(Target, Bundle, NodeRowsByKey, ReadWarnings, OutPlan, Policies);
}

void QuestBundle_DiffInstancedChildren(const UObject* Owner, const FString& OwnerKey, const FQuestDataBundle& Bundle, FQuestNodePlanEntry& Entry, FQuestInPlacePlan& OutPlan)
{
	DiffInstancedChildren(Owner, OwnerKey, Bundle, Entry, OutPlan, FQuestAbsentPolicyResolver());
}

int32 QuestBundle_ApplyChangesToObject(UObject* Owner, const FString& OwnerKey, const TArray<FQuestPropertyChange>& Changes, TArray<FString>& OutSkipped)
{
	return ApplyChangesToObject(Owner, OwnerKey, Changes, OutSkipped);
}

void QuestBundle_ApplyPlan(UQuestlineGraph& Target, const FQuestInPlacePlan& Plan, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, FQuestApplyResult& OutResult, const FQuestApplyOptions& Options)
{
	ApplyPlan(Target, Plan, Bundle, NodeRowsByKey, OutResult, Options);
}

static FAutoConsoleCommand GImportQuestlineCmd(
	TEXT("SimpleQuest.ImportQuestline"),
	TEXT("PROTOTYPE: reconstruct a questline asset from an interlingua table folder (an ExportQuestline output) and "
		"compile it. Creates a fresh <QuestlineID>_RT asset. Args: <FolderPath> <DestPackagePath> (e.g. "
		"\"E:/.../Saved/QuestExport/QL_Ch1_BasicTrigger\" /Game/Imported)."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ImportQuestlineCmd));

static FAutoConsoleCommand GEnumerateSourceColumnsCmd(
	TEXT("SimpleQuest.EnumerateSourceColumns"),
	TEXT("PROTOTYPE: list the columns a foreign source exposes (proves the source-column provider seam). "
		"Args: <SourceFolder> [--format=<name>] (default TSV)."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("EnumerateSourceColumns: usage <SourceFolder> [--format=<name>]"));
			return;
		}
		// The console tokenizes on whitespace and strips quotes, so a path with spaces arrives as MULTIPLE args. Re-join all
		// non-flag args with spaces to reconstruct the folder (quoted or not); --format=<name> is the only recognized flag.
		FString Folder;
		FString FormatName = TEXT("TSV");
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--format=")))
			{
				FormatName = Arg.RightChop(9);
			}
			else
			{
				if (!Folder.IsEmpty()) Folder += TEXT(" ");
				Folder += Arg;
			}
		}
		Folder = Folder.TrimStartAndEnd().TrimQuotes();   // tolerate stray outer quotes / padding if any survived

		const FQuestSourceColumns Cols = EnumerateForeignFileColumns(FormatName, Folder);
		if (!Cols.bReadable)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("EnumerateSourceColumns: %s"), *Cols.Error.ToString());
			return;
		}
		FString Joined;
		for (const FName& C : Cols.Columns) Joined += (Joined.IsEmpty() ? TEXT("") : TEXT(", ")) + C.ToString();
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("EnumerateSourceColumns: %d column(s)%s: %s"),
			Cols.Columns.Num(), Cols.bHasDuplicateColumns ? TEXT(" [DUPLICATE]") : TEXT(""), *Joined);
	}));
