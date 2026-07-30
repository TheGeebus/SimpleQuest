// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// PROTOTYPE — Resolver, Phase 2 import (the round-trip's second half). Reconstructs AUTHORED editor nodes from the
// interlingua table folder an export produced, then feeds the EXISTING compiler — never reverses the compiler. Creates
// a FRESH asset (QuestlineID suffixed _RT so its compiled tag namespace doesn't collide with the original), so the
// round-trip is verifiable by the two oracles: C (re-export this asset, diff the folders modulo _RT) and B2
// (compile + DumpCompiled both, diff modulo the tag prefix). Console-triggered, editor-only. Not shipped API.

#include "CoreMinimal.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "IAssetTools.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/Paths.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestReflectionUtils.h"
#include "Settings/SimpleQuestSettings.h"
#include "UObject/UnrealType.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "Internationalization/Text.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Factories/QuestlineGraphFactory.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "ISimpleQuestEditorModule.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestMappingSource.h"
#include "Utilities/QuestlineGraphCompiler.h"

namespace
{
	// The import routing core speaks the shared, format-free FQuestDataBundle (Resolver/QuestDataBundle.h). The local
	// FImport* bundle structs + the TSV parsing (Unsanitize / ParseTable / ParseEdges) moved to the TSV provider
	// (Resolver/TsvQuestDataFormat::ReadBundle) in Stage 2 — the routing core never touches a file or format. What was
	// ReadAndValidate splits: the provider reads the folder into a bundle; ValidateBundle (below) does the structural
	// checks on that already-parsed bundle.

	// P0 (routing half) — structural validation of an ALREADY-READ bundle. File reading (folder discovery + TSV parse)
	// is the provider's job (ReadBundle); this does ONLY the structural checks and builds the two lookup indices the
	// later phases need. Refuse (return false) on any inconsistency so no partial asset is ever created (validate-
	// upfront). Provider-agnostic: a malformed bundle from ANY format provider is refused here identically.
	bool ValidateBundle(const FQuestDataBundle& Bundle,
	                    TMap<FString, const FQuestDataRow*>& NodeRowsByKey,   // node/self key -> row (excludes child rows)
	                    TSet<FString>& AllRowKeys,                            // every key incl. instanced child keys
	                    FString& OutError)
	{
		// The questline-self table is keyed "questline_graph" — required, exactly one row.
		const FQuestDataTable* Questline = Bundle.TablesByType.Find(TEXT("questline_graph"));
		if (!Questline) { OutError = TEXT("no questline_graph table (the self row)"); return false; }
		if (Questline->Rows.Num() != 1) { OutError = TEXT("questline_graph table must have exactly one row"); return false; }

		// Index every row key. Node/self rows are keyed by GUID digits or the EffectiveID; instanced child rows carry
		// a '/' path segment. Only NODE rows spawn editor nodes, so split the two — but track ALL keys so edge
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

		// Validate exactly one Entry row per graph cell (each graph level has one Entry — the import adopts it).
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
	// the graph would have — a combinator row + operand edges in + a feeds-prereq edge out — into the neutral bundle,
	// then the ordinary P0-P5 pipeline compiles it (no new compiler path; combinators store NO operands, the wiring is
	// emergent from edges — same shape Ch6 round-trips green). Routing-core + format-agnostic: identical for TSV/JSON.
	// The convention is a WRITE-IN only; export always emits the explicit structural form (so "graph is sugar" the
	// reverse way — a re-export of an unlock_after import shows prerequisite_and rows, never unlock_after).
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
	// not new control flow. AND's output pin is "Out"; OR's (and NOT's) is "PrereqOut" — verified vs AllocateDefaultPins
	// + the Ch6 export edges — which is why the output verb is a per-row field rather than hardcoded.
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
	// canonical property names — applying a binding only where the resolved class actually has that property (bind once,
	// land where it fits). Runs before ApplyFlowConventions/ValidateBundle so the class-driven pipeline sees canonical
	// rows. The questline_graph self table is left untouched (it isn't a fanned-out source table).
	bool ApplyMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
	{
		if (Mapping.DiscriminatorColumn.IsNone())
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("ImportQuestline: mapping has no discriminator column set — malformed mapping, refusing."));
			return false;
		}
		const FString DiscCol = Mapping.DiscriminatorColumn.ToString();

		// Extract the actual source shape from the bundle we're about to transform (no drift — this IS the data), then run
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
		if (!ValidateMappingAgainstSource(Mapping, ActualColumns, ActualDiscriminatorValues, GuardErrors))
		{
			for (const FText& E : GuardErrors)
				UE_LOG(LogSimpleQuest, Error, TEXT("ImportQuestline mapping guard: %s"), *E.ToString());
			return false;   // refuse — no partial asset from an unsafe mapping
		}

		// The routing class map — the SAME shared builder the guard used, so membership can't drift. The guard already
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
					Warnings.Add(FString::Printf(TEXT("mapping: row '%s' has %s='%s' with no class mapping — row not routed"),
						*Row.Key, *DiscCol, *DiscValue));
					continue;
				}
				UClass* RowClass = *Found;
				Row.Cells.Add(TEXT("class"), FQuestDataValue::MakeString(RowClass->GetName()));   // short name (TryFindTypeSlow form)
				Row.Cells.Remove(DiscCol);   // the discriminator column isn't a node property
				const TSet<FName>& RowProps = PropsByClass[RowClass];

				// 2. Rename each bound source column to its canonical property name — but only when this class has that
				//    property (bind once, land where it fits). A binding that doesn't fit this class is simply skipped.
				for (const FQuestColumnBinding& B : Mapping.Bindings)
				{
					if (!RowProps.Contains(B.TargetProperty)) continue;   // property not on this class — binding doesn't apply
					const FString SrcCol = B.SourceColumn.ToString();
					FQuestDataValue* Cell = Row.Cells.Find(SrcCol);
					if (!Cell) continue;   // source column absent on this row — the absent-policy is handled downstream
					FQuestDataValue Moved = MoveTemp(*Cell);
					Row.Cells.Remove(SrcCol);
					Row.Cells.Add(B.TargetProperty.ToString(), MoveTemp(Moved));
				}
				++Routed;
			}
		}
		if (Routed > 0)
			UE_LOG(LogSimpleQuest, Log, TEXT("ImportQuestline: mapping routed + renamed %d row(s)."), Routed);
		return true;
	}
	
	// --------------------------------------------------------------------------------------------------------------

	// Synthesize the structural prereq form for every convention cell found on any node row, then strip the cell so it
	// never reaches RestoreCell as a bogus property. Runs on the bundle AFTER ReadBundle and BEFORE ValidateBundle, so
	// the synthesized edges' endpoints are checked by the ordinary endpoint guard (a typo'd operand key is refused with
	// no half-built asset). PrerequisiteAnd has a floor of 2 condition pins, so ConditionPinCount = max(N, 2) — a
	// single operand wires one pin and leaves one free (legal; ResolveDestPin only fills FREE condition pins).
	void ApplyFlowConventions(FQuestDataBundle& Bundle, TArray<FString>& Warnings)
	{
		// Synthesized rows/edges are STAGED into locals and applied AFTER the scan — never insert into Bundle.TablesByType
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
				// A content node's Prerequisites input is single-link (the schema disallows a second wire — combine with
				// AND/OR instead). So at most ONE flow-convention may apply per row; a second populated convention column
				// is a source authoring error. Honor the FIRST (table order), strip + warn the rest — never synthesize two
				// combinators into one input (that can't round-trip and the compiled model would be undefined).
				bool bRowConsumed = false;
				for (const FFlowConvention& Conv : GFlowConventions)
				{
					const FQuestDataValue* Cell = Row.Cells.Find(Conv.Column);
					if (!Cell) continue;
					if (bRowConsumed)
					{
						Warnings.Add(FString::Printf(TEXT("'%s' carries %s in addition to an earlier prerequisite convention — "
							"a node's Prerequisites input takes only one; ignoring %s (combine operands within a single convention)"),
							*Row.Key, Conv.Column, Conv.Column));
						Row.Cells.Remove(Conv.Column);
						continue;
					}

					TArray<FString> Operands = ParseFlowKeyList(*Cell);
					Row.Cells.Remove(Conv.Column);   // strip regardless — a declared-but-empty gate is still not a property
					if (Operands.Num() == 0)
					{
						Warnings.Add(FString::Printf(TEXT("%s on '%s' listed no operands — no prerequisite synthesized"),
							Conv.Column,
							*Row.Key));
						continue;
					}
					// Operand cap: NOT negates exactly one operand. If a source over-lists, keep the first + warn (refusing
					// the whole gate would be harsher than honoring the clear intent of the first operand).
					if (Operands.Num() > Conv.MaxOperands)
					{
						Warnings.Add(FString::Printf(TEXT("%s on '%s' lists %d operands but takes at most %d — using the first, ignoring the rest"),
							Conv.Column, *Row.Key, Operands.Num(), Conv.MaxOperands));
						Operands.SetNum(Conv.MaxOperands);
					}

					// Combinator key: derived from the GATED row + column so re-imports are stable and two gates never
					// collide. Needn't be a GUID — SpawnNodeFromRow mints a deterministic FGuid from any non-GUID key.
					const FString CombKey = Row.Key + TEXT("__") + FString(Conv.Column);
					const FString GraphCell = Row.Get(TEXT("graph"));   // same graph level as the gated node

					FQuestDataRow CombRow;
					CombRow.Key = CombKey;
					CombRow.Cells.Add(TEXT("class"),  FQuestDataValue::MakeString(Conv.CombinatorClass));
					CombRow.Cells.Add(TEXT("graph"),  FQuestDataValue::MakeString(GraphCell));
					// ConditionPinCount only for variadic combinators (AND/OR). NOT's single input pin is fixed at
					// AllocateDefaultPins with no count property — emitting the cell would be a stray column.
					if (Conv.bHasConditionPinCount)
						CombRow.Cells.Add(TEXT("ConditionPinCount"), FQuestDataValue::MakeNumber(FString::FromInt(FMath::Max(Operands.Num(), 2))));
					RowsToAddByStem.FindOrAdd(Conv.TableStem).Add(MoveTemp(CombRow));

					for (const FString& OpKey : Operands)
						EdgesToAdd.Add({ OpKey, TEXT("activates(Any Outcome)"), CombKey });
					EdgesToAdd.Add({ CombKey, FString(Conv.OutEdgeType), Row.Key });

					++Synthesized;
					bRowConsumed = true;
					UE_LOG(LogSimpleQuest, Verbose, TEXT("ImportQuestline: flow-convention %s on '%s' -> %s '%s' gating via %d operand(s)"),
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
				// actually carry — NOT rows omit ConditionPinCount, so a hardcoded list would seed a phantom column that
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
			UE_LOG(LogSimpleQuest, Log, TEXT("ImportQuestline: synthesized %d prerequisite combinator(s) from flow conventions."), Synthesized);
	}
	// --------------------------------------------------------------------------------------------------------------

	// Select the format provider: a --format=<name> console argument (highest priority), else the project default,
	// else "TSV". The per-mapping format source is empty here for now. Returns null when a named format isn't
	// registered, so the caller refuses before creating anything.
	TUniquePtr<ISimpleQuestDataFormat> MakeQuestDataFormat(const TArray<FString>& Args)
	{
		FString ConsoleArgName;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--format=")))
			{
				ConsoleArgName = Arg.RightChop(9);   // length of "--format="
				break;
			}
		}
		const FString SettingsDefault = GetDefault<USimpleQuestSettings>()->DefaultImportFormat.ToString();

		FString Error;
		const FString Name = ResolveQuestDataFormatName(ConsoleArgName, /*MappingAsset*/ FString(), SettingsDefault, Error);
		if (Name.IsEmpty())
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("ImportQuestline: %s. No asset created."), *Error);
			return nullptr;
		}
		return FQuestDataFormatRegistry::Get().Create(Name);
	}

	// Optional --mapping=<asset path>: loads a studio's source-shape translation. Null when no --mapping arg is present
	// (the source is already in our shape — our own round-trip / export-as-teacher — so no mapping is needed).
	const UQuestImportMapping* LoadMappingArg(const TArray<FString>& Args)
	{
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--mapping=")))
			{
				return LoadObject<UQuestImportMapping>(nullptr, *Arg.RightChop(10));   // length of "--mapping="
			}
		}
		return nullptr;
	}

	void RestoreCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value);   // fwd decl (Array recurses)

	// Restore a Kind=Array cell into the destination container. The Kind erases container type (TArray vs TSet), so branch
	// on the destination Prop. Each element recurses through RestoreCell by ITS own Kind. NOTE: this path is exercised only
	// by a STRUCTURED provider (JSON); TSV delivers arrays as a single Kind=Scalar cell (the "(a,b)" literal -> ImportText),
	// so TSV never reaches here — no regression risk. FGameplayTagContainer arriving as an Array (a JSON tag list) is also
	// handled: a TagContainer destination isn't an FArray/FSetProperty, so it falls to the ImportText fallback on the
	// element-joined literal — but in practice JSON emits FGameplayTagContainer as Kind=TagContainer, not Array.
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
		// Unexpected destination for an Array Kind — leave default (defensive; a structured provider shouldn't emit this).
	}

	// Restore one property from a structured cell value. switch(Kind): a STRUCTURED provider (JSON) delivers typed Kinds
	// (Tag/Text/Bool/Array) that write directly to the property from the typed field; the string-carrying Kinds (Scalar/
	// Enum/Reference/StructLiteral) go through ImportText from Value.StringForm. The TSV provider produces Kind=Scalar for
	// EVERY cell (including FText cells, which arrive as a Scalar holding the NSLOCTEXT string), so the Scalar arm MUST
	// preserve the property-type FText branch (ReadFromBuffer) that the pre-Stage-3 code used — otherwise TSV FText cells
	// regress from ReadFromBuffer to ImportText. This keeps the refactor byte-identical for TSV (the B2 no-regression gate).
	void RestoreCell(const FProperty* Prop, void* ValuePtr, const FQuestDataValue& Value)
	{
		switch (Value.Kind)
		{
		case EQuestDataValueKind::Empty:
			return;   // leave the constructed default (Q6 symmetry — replaces the old CellText.IsEmpty() skip)

		case EQuestDataValueKind::Tag:
			if (CastField<FStructProperty>(Prop))
			{
				*static_cast<FGameplayTag*>(ValuePtr) = Value.Tag;   // typed; inverse of the export reinterpret read
			}
			return;

		case EQuestDataValueKind::TagContainer:
			if (CastField<FStructProperty>(Prop))
			{
				*static_cast<FGameplayTagContainer*>(ValuePtr) = Value.TagContainer;
			}
			return;

		case EQuestDataValueKind::Text:
			if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
			{
				TextProp->SetPropertyValue(ValuePtr, Value.Text);   // typed — carries loc ns/key, no buffer round-trip
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
			// String-carrying Kinds — all route through StringForm -> ImportText, which types against the property (a
			// numeric property parses "3" fine; the Number/String distinction is a provider-render concern, not a
			// restore concern). Value.StringForm holds the cell string (== today's CellText for the TSV path). The FText
			// special-case is PRESERVED here because TSV delivers FText cells as Kind=Scalar (a Scalar holding NSLOCTEXT);
			// routing them to ImportText instead of ReadFromBuffer would regress TSV. JSON never lands here for FText (it
			// uses Kind=Text above), so this branch only ever sees TSV's FText-as-Scalar — exactly the old behavior.
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
	// and any column with no matching property (defensive — a stale table column shouldn't abort the import).
	void RestoreRowProperties(UObject* Target, const FQuestDataRow& Row)
	{
		for (const TPair<FString, FQuestDataValue>& Cell : Row.Cells)
		{
			const FString& Col = Cell.Key;
			if (Col == TEXT("class") || Col == TEXT("graph")) continue;
			FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*Col));
			if (!Prop) continue;
			// RestoreCell types the value against the property (structured providers write typed fields directly; string-
			// carrying Kinds route through ImportText on Scalar) — see the switch(Kind) in RestoreCell.
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

	// Rebuild one instanced child object from its row: NewObject<class> under Owner, restore its cells, recurse its
	// own instanced properties. Returns the constructed object (or null if the row/class is missing).
	UObject* BuildChildObject(UObject* Owner, const FString& ChildKey, const FQuestDataBundle& Bundle,
							  TSet<FString>& OutConsumed, TArray<FString>& OutWarnings)
	{
		FString ClassName;
		const FQuestDataRow* Row = FindChildRow(Bundle, ChildKey, ClassName);
		if (!Row) { OutWarnings.Add(FString::Printf(TEXT("child row missing for key '%s'"), *ChildKey)); return nullptr; }

		UClass* Class = UClass::TryFindTypeSlow<UClass>(ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
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
		// TryFindTypeSlow resolves a class by short name across loaded packages — robust to the class living in any
		// module (not just SimpleQuestEditor), which the hardcoded /Script/ prefix assumed. Same resolver the reward
		// child classes use, so node + sub-object class resolution stay uniform.
		UClass* Class = UClass::TryFindTypeSlow<UClass>(ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
		if (!Class) { Warnings.Add(FString::Printf(TEXT("unknown node class '%s' for key '%s'"), *ClassName, *Row.Key)); return nullptr; }

		UEdGraphNode* Node = NewObject<UEdGraphNode>(TargetGraph, Class, NAME_None, RF_Transactional);
		TargetGraph->AddNode(Node, /*bFromUI*/ false, /*bSelectNewNode*/ false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();

		// Adopt identity AFTER the placement hooks (which regenerate GUID + sweep the label). Dual-key contract: a row
		// key that parses as a GUID is one of OUR exports — preserve it verbatim (round-trip identity). A key that does
		// NOT parse is a fresh-authoring semantic id (a studio's "kill_boss") — mint a DETERMINISTIC GUID from it so the
		// identity is stable across re-imports (save data + cross-asset refs + in-place round-trip don't churn).
		// NewDeterministicGuid is a pure name-based hash (no process/build state), so the same id always mints the same
		// GUID. Edge wiring is unaffected: NodeByKey is keyed by the row-key STRING, never this GUID.
		if (UQuestlineNodeBase* QNode = Cast<UQuestlineNodeBase>(Node))
		{
			FGuid ParsedGuid;
			if (FGuid::Parse(Row.Key, ParsedGuid))
			{
				QNode->QuestGuid = ParsedGuid;   // round-trip: preserve our exported GUID verbatim
			}
			else
			{
				// Fresh authoring: mint a stable GUID from the semantic key, namespaced so it can't collide with a
				// different consumer's deterministic GUIDs.
				QNode->QuestGuid = FGuid::NewDeterministicGuid(FString(TEXT("SimpleQuest.Import.")) + Row.Key);
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
	// exported rows. Safe: CreateInnerGraph / the factory only use the default Entry as a starting affordance — nothing
	// retains it; the graph's Entry is always re-found by class (verified: CreateInnerGraph holds no ref, callers scan
	// Graph->Nodes for the Entry type). Schema + (for inner graphs) the change subscription are unaffected.
	void ClearDefaultNodes(UEdGraph* Graph)
	{
		if (!Graph) return;
		TArray<UEdGraphNode*> ToRemove = Graph->Nodes;   // copy — RemoveNode mutates the array
		for (UEdGraphNode* N : ToRemove)
		{
			if (N) Graph->RemoveNode(N);
		}
	}

	// Import every node row belonging to one graph level (graph cell). Quest containers, once spawned + Finalized,
	// have auto-created inner graphs whose auto-Entry we adopt by GUID-overwrite from that inner graph's Entry row —
	// then recurse into the inner level. GraphCell is "root" for the top graph, else the container node's key.
	void ImportGraphLevel(UEdGraph* TargetGraph, const FString& GraphCell, const FQuestDataBundle& Bundle,
						  const TMap<FString, const FQuestDataRow*>& NodeRowsByKey,
						  TMap<FString, UEdGraphNode*>& NodeByKey, TSet<FString>& Consumed, TArray<FString>& Warnings)
	{
		ClearDefaultNodes(TargetGraph);   // populate entirely from rows (incl. the exported Entry) — no double-Entry

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
			// bShowDeactivationPins is true — but that ran BEFORE the property restore, so it was skipped. Both content
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

	// Resolve an output pin on the source node by the edge type's parenthesized qualifier (the source pin name the
	// export wrote). Falls back to category matching if the exact name isn't found (defensive).
	UEdGraphPin* ResolveSourcePin(UEdGraphNode* Node, const FString& EdgeType)
	{
		// EdgeType is "verb(PinName)" — extract PinName.
		FString PinName;
		int32 Open, Close;
		if (EdgeType.FindChar(TEXT('('), Open) && EdgeType.FindLastChar(TEXT(')'), Close) && Close > Open)
			PinName = EdgeType.Mid(Open + 1, Close - Open - 1);

		for (UEdGraphPin* Pin : Node->Pins)
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinName.ToString() == PinName) return Pin;
		return nullptr;
	}

	// The DESTINATION input category isn't always the source output category. Outcome outputs (QuestOutcome) route
	// into a target's activation-style input (QuestActivation) — an Exit's "Outcome" pin, or a content node's
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
	// category — because the graph legitimately connects across categories: a Step's QuestActivation "Any Outcome"
	// output AND a NOT's QuestPrerequisite "PrereqOut" output can BOTH feed a combinator's QuestPrerequisite
	// Condition_N input (UE's schema permits it — that's how "step completion satisfies a prereq condition" is
	// authored). Priority:
	//   1. A prereq Condition_N input (combinators): ANY source category routes here — take the first free slot.
	//   2. Else the single input of the source-derived category (outcome/activation -> Activate; prereq -> a
	//      Prerequisites input; deactivate -> Deactivate).
	UEdGraphPin* ResolveDestPin(UEdGraphNode* Node, FName SourceCategory)
	{
		// 1. Combinator condition input — category-agnostic on the source side (a prereq input accepts outcome,
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
			// contains edges are NOT wiring — they're the instanced-reattach record. Cross-check (D1's completeness
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

			SourcePin->MakeLinkTo(DestPin);   // raw link — reconstruct-known-topology, no schema side effects
			UE_LOG(LogSimpleQuest, Verbose, TEXT("ImportQuestline: wired [%s] %s(%s) -> [%s] %s(%s)"),
				*E.From, *SourcePin->PinName.ToString(), *SourcePin->PinType.PinCategory.ToString(),
				*E.To, *DestPin->PinName.ToString(), *DestPin->PinType.PinCategory.ToString());
		}
	}

	void ImportQuestlineCmd(const TArray<FString>& Args)
	{
		// Separate the optional "--format=<name>" arg from the positional path args BEFORE rejoining (it must not get
		// swept into the space-rejoined folder path). PathArgs = every arg that isn't a --flag.
		TArray<FString> PathArgs;
		for (const FString& Arg : Args)
		{
			if (!Arg.StartsWith(TEXT("--"))) PathArgs.Add(Arg);
		}
		if (PathArgs.Num() < 2)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ImportQuestline: usage 'SimpleQuest.ImportQuestline <FolderPath> <DestPackagePath> [--format=json]'."));
			return;
		}
		// Console arg tokenization splits on whitespace and does NOT honor quotes, so a folder path containing spaces
		// (e.g. "E:/Unreal Projects/...") arrives as multiple Args. The dest package path is the LAST path arg (never has
		// spaces — it's a /Game/... mount path); the folder path is everything before it, rejoined with spaces.
		const FString DestPackagePath = PathArgs.Last();
		TArray<FString> FolderParts = PathArgs;
		FolderParts.Pop();                                    // drop the dest path
		FString FolderPath = FString::Join(FolderParts, TEXT(" "));
		FolderPath = FolderPath.TrimQuotes();                 // tolerate quotes if the caller added them

		const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args);
		if (!Format)
		{
			return;   // the unregistered-format error was already logged; refuse before creating anything.
		}
		FQuestDataBundle Bundle;
		if (!Format->ReadBundle(FolderPath, Bundle))
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("ImportQuestline: could not read '%s' as %s. No asset created."), *FolderPath, *Format->FormatName());
			return;
		}

		TArray<FString> Warnings;

		// A studio's source-shape translation (optional). Absent = the source is already in our shape (our own
		// round-trip / export-as-teacher), so no mapping is needed. Runs before flow-conventions so a mapped column can
		// feed a convention (e.g. a source column mapped onto unlock_after).
		if (const UQuestImportMapping* Mapping = LoadMappingArg(Args))
		{
			if (!ApplyMapping(Bundle, *Mapping, Warnings))
			{
				UE_LOG(LogSimpleQuest, Error, TEXT("ImportQuestline: mapping guard refused the import. No asset created."));
				return;
			}
		}
		ApplyFlowConventions(Bundle, Warnings);

		TMap<FString, const FQuestDataRow*> NodeRowsByKey;
		TSet<FString> AllRowKeys;
		FString Error;
		if (!ValidateBundle(Bundle, NodeRowsByKey, AllRowKeys, Error))
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("ImportQuestline: validation failed — %s. No asset created."), *Error);
			return;
		}

		// P1 — create the asset via the factory, then restore the self row (with _RT identity + instanced rewards).
		// Two distinct identities: the ROW KEY (sanitized EffectiveID — folder name, tag namespace) and the authored
		// QuestlineID FIELD (raw, whatever the designer typed, spaces and all — the compiler sanitizes it only when
		// building tags, never mutating the field). The asset NAME rides the sanitized key (a package name can't hold
		// spaces); the QuestlineID FIELD must preserve the raw authored value so the round-trip doesn't alter it.
		const FQuestDataRow& SelfRow = Bundle.TablesByType[TEXT("questline_graph")].Rows[0];
		const FString OriginalKey = SelfRow.Key;                          // sanitized — folder/tag identity
		const FString RawQuestlineID = SelfRow.Get(TEXT("QuestlineID"));  // raw authored field (may be empty)
		const FString AssetName = OriginalKey + TEXT("_RT");              // _RT so the compiled tag namespace doesn't collide.

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UQuestlineGraphFactory* Factory = NewObject<UQuestlineGraphFactory>();
		UObject* Created = AssetTools.CreateAsset(AssetName, DestPackagePath, UQuestlineGraph::StaticClass(), Factory);
		UQuestlineGraph* Graph = Cast<UQuestlineGraph>(Created);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("ImportQuestline: asset creation failed at '%s/%s'."), *DestPackagePath, *AssetName);
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
			//     fallback yields "<name>_RT" — matching the source modulo _RT. Writing the literal "_RT" here (the
			//     prior bug) would make QuestlineID = "_RT", tags = SimpleQuest.Questline._RT.*, and the export folder
			//     "_RT" — diverging from the asset-name identity the source actually used.
			if (!RawQuestlineID.IsEmpty())
			{
				if (FProperty* IDProp = Graph->GetClass()->FindPropertyByName(TEXT("QuestlineID")))
				{
					const FString RT = RawQuestlineID + TEXT("_RT");
					IDProp->ImportText_Direct(*RT, IDProp->ContainerPtrToValuePtr<void>(Graph), nullptr, PPF_None);
				}
			}
			// else: RestoreRowProperties already left it empty (the source cell was empty) — nothing to do.
		}
		ReattachInstanced(Graph, OriginalKey, Bundle, Consumed, Warnings);   // self-row child keys are prefixed by the self key

		// P2 — spawn nodes, root graph first, recursing into container inner graphs.
		TMap<FString, UEdGraphNode*> NodeByKey;
		ImportGraphLevel(Graph->QuestlineEdGraph, TEXT("root"), Bundle, NodeRowsByKey, NodeByKey, Consumed, Warnings);

		// P3 — pin refresh pass (innermost-first).
		RefreshPinsPass(Bundle, NodeRowsByKey, NodeByKey, Warnings);

		// P4 — wire edges + contains-edge cross-check.
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

		for (const FString& W : Warnings) UE_LOG(LogSimpleQuest, Warning, TEXT("ImportQuestline: %s"), *W);
		UE_LOG(LogSimpleQuest, Log, TEXT("ImportQuestline: '%s' -> '%s/%s' — %d node(s), %d edge(s), %d warning(s), compile %s. Run C (re-export + diff) and B2 (DumpCompiled + diff) to verify."),
			*OriginalKey, *DestPackagePath, *AssetName, NodeByKey.Num(), Bundle.Edges.Num(), Warnings.Num(), bCompiled ? TEXT("OK") : TEXT("FAILED"));
	}
}

static FAutoConsoleCommand GImportQuestlineCmd(
	TEXT("SimpleQuest.ImportQuestline"),
	TEXT("PROTOTYPE: reconstruct a questline asset from an interlingua table folder (an ExportQuestline output) and "
		"compile it. Creates a fresh <QuestlineID>_RT asset. Args: <FolderPath> <DestPackagePath> (e.g. "
		"\"E:/.../Saved/QuestExport/QL_Ch1_BasicTrigger\" /Game/Imported)."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ImportQuestlineCmd));

static FAutoConsoleCommand GEnumerateSourceColumnsCmd(
	TEXT("SimpleQuest.EnumerateSourceColumns"),
	TEXT("PROTOTYPE: list the columns a mapping's source exposes (proves the source-column provider seam). "
		"Arg: the UQuestImportMapping asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("EnumerateSourceColumns: usage <mapping asset path>"));
			return;
		}
		const UQuestImportMapping* Mapping = LoadObject<UQuestImportMapping>(nullptr, *Args[0]);
		if (!Mapping) { UE_LOG(LogSimpleQuest, Error, TEXT("EnumerateSourceColumns: couldn't load '%s'."), *Args[0]); return; }

		const FQuestSourceColumns Cols = EnumerateMappingSourceColumns(*Mapping);
		if (!Cols.bReadable)
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("EnumerateSourceColumns: %s"), *Cols.Error.ToString());
			return;
		}
		FString Joined;
		for (const FName& C : Cols.Columns) Joined += (Joined.IsEmpty() ? TEXT("") : TEXT(", ")) + C.ToString();
		UE_LOG(LogSimpleQuest, Log, TEXT("EnumerateSourceColumns: %d column(s)%s: %s"),
			Cols.Columns.Num(), Cols.bHasDuplicateColumns ? TEXT(" [DUPLICATE]") : TEXT(""), *Joined);
	}));
