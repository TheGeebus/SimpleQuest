// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


// PROTOTYPE — Resolver, Phase 2 authored-graph export. Serializes a questline's AUTHORED model as the interlingua
// folder: one entity table per node/sub-object type (reflection-driven — every EditAnywhere non-Transient UPROPERTY;
// instanced sub-objects explode to child rows in their own type tables) plus one knot-collapsed edge table where
// routing, prereq wiring, deactivation, and nesting are all {from, type, to}. Quest containers' inner graphs recurse;
// LinkedQuestline placements do NOT (the LinkedGraph soft path column is the cross-folder foreign key — the linked
// asset's content belongs to its own export). This is the lossless-structured interlingua form (NOT a readable
// projection): machine fields expected, prettiness is a later panel concern. Read-only, console-triggered. Not shipped API.

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	// Escape tabs/newlines in a serialized property value so a value with embedded whitespace can't break the TSV row.
	FString Sanitize(const FString& In)
	{
		return In.Replace(TEXT("\t"), TEXT("\\t")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("\\n"));
	}

	// ---- Bundle shapes: accumulate the whole export, then write, so determinism lives in one place and a failed walk writes nothing.

	// One entity row destined for a per-type table: the row key + serialized property cells keyed by column name.
	// Cells are keyed (not positional) at collect time; WriteBundle lays them out against the type's column list.
	struct FExportRow
	{
		FString Key;
		TMap<FString, FString> Cells;
	};

	// One per-type entity table: ordered column names (captured once per class from reflection, so every row of a type
	// shares the identical column set) + accumulated rows.
	struct FExportTable
	{
		TArray<FString> Columns;
		TArray<FExportRow> Rows;
	};

	// One typed edge in the unified relationship table. Type carries the parenthesized qualifier (pin name / property path).
	struct FExportEdge
	{
		FString From;
		FString Type;
		FString To;
	};

	// Everything one export run accumulates before any file is written.
	struct FExportBundle
	{
		TMap<FString, FExportTable> TablesByType;    // key = table file stem (snake_cased short type name)
		TArray<FExportEdge> Edges;
		int32 KnotsCollapsed = 0;                    // knot NODES suppressed — logging signal that collapse ran
	};

	// Table file stem for a class: strip the QuestlineNode_ prefix, snake_case the remainder. Underscores insert only on a
	// lower→upper boundary so acronym runs stay together. Derived display, not identity — collisions can't occur because
	// class names are unique and the transform is injective enough for this corpus.
	FString TypeStem(const UClass* Class)
	{
		FString Name = Class->GetName();
		Name.RemoveFromStart(TEXT("QuestlineNode_"));
		FString Out;
		for (int32 i = 0; i < Name.Len(); ++i)
		{
			const TCHAR C = Name[i];
			if (FChar::IsUpper(C) && i > 0 && !FChar::IsUpper(Name[i - 1]))
			{
				Out.AppendChar(TEXT('_'));
			}
			Out.AppendChar(FChar::ToLower(C));
		}
		return Out;
	}

	// Node row key — QuestGuid digits. Every UQuestlineNodeBase carries QuestGuid (base-class field), so no fallback needed.
	FString NodeKeyOf(const UQuestlineNodeBase* Node)
	{
		return Node->QuestGuid.ToString(EGuidFormats::Digits);
	}

	// True when a property's value(s) are Instanced UObjects — the shapes that must explode to child rows instead of
	// serializing as a dangling object path. Recurses array inners, map values, and struct fields so container-wrapped
	// instanced data (e.g. TMap<FGameplayTag, FQuestRewardSet> wrapping an instanced array) classifies correctly.
	bool IsInstancedBearing(const FProperty* Prop)
	{
		if (const FObjectProperty* Obj = CastField<FObjectProperty>(Prop))
		{
			return Obj->HasAnyPropertyFlags(CPF_InstancedReference);
		}
		if (const FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
		{
			return IsInstancedBearing(Arr->Inner);
		}
		if (const FMapProperty* Map = CastField<FMapProperty>(Prop))
		{
			return IsInstancedBearing(Map->ValueProp);
		}
		if (const FStructProperty* Struct = CastField<FStructProperty>(Prop))
		{
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				if (IsInstancedBearing(*It))
				{
					return true;
				}
			}
		}
		return false;
	}

	// Serialize one non-instanced property value to a cell string. FText routes through FTextStringHelper (loc-preserving,
	// quote-wrapped — the same convention as the compiled display ini, so one FText form governs every file we write);
	// everything else through ExportTextItem (T3D's conversion, round-trip-faithful via ImportText).
	FString SerializeCell(const FProperty* Prop, const void* ValuePtr)
	{
		FString Out;
		if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			FTextStringHelper::WriteToBuffer(Out, TextProp->GetPropertyValue(ValuePtr), /*bRequiresQuotes*/ true, /*bStripPackageNamespace*/ false);
			return Out;
		}
		Prop->ExportTextItem_Direct(Out, ValuePtr, /*Default*/ nullptr, /*Parent*/ nullptr, PPF_None);
		return Out;
	}

	void CollectEntityRow(const UObject* Entity, const FString& Key, const TMap<FString, FString>& ExtraCells, FExportBundle& Bundle);

	// Emit child rows + contains edges for every instanced object reachable from Prop on the entity keyed OwnerKey.
	// PathPrefix is the property path so far relative to OwnerKey (e.g. "Rewards" or "QuestlineRewards[<key>].Rewards");
	// it becomes both the contains-edge qualifier and the child row's synthetic key suffix, so edge and key corroborate.
	void RecurseInstanced(const FProperty* Prop, const void* ValuePtr, const FString& OwnerKey, const FString& PathPrefix, FExportBundle& Bundle)
	{
		// Direct instanced object: one child row. Null slot = no row, no edge — absence is the honest representation.
		if (const FObjectProperty* Obj = CastField<FObjectProperty>(Prop))
		{
			if (const UObject* Sub = Obj->GetObjectPropertyValue(ValuePtr))
			{
				const FString ChildKey = FString::Printf(TEXT("%s/%s"), *OwnerKey, *PathPrefix);
				Bundle.Edges.Add({ OwnerKey, FString::Printf(TEXT("contains(%s)"), *PathPrefix), ChildKey });
				CollectEntityRow(Sub, ChildKey, /*ExtraCells*/ {}, Bundle);
			}
			return;
		}
		// Array: recurse each element with an [i] path segment.
		if (const FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(Arr, ValuePtr);
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				RecurseInstanced(Arr->Inner, Helper.GetRawPtr(i), OwnerKey, FString::Printf(TEXT("%s[%d]"), *PathPrefix, i), Bundle);
			}
			return;
		}
		// Map: recurse each VALUE with a [KeyExport] path segment (corpus case: QuestlineRewards keyed by outcome tag).
		if (const FMapProperty* Map = CastField<FMapProperty>(Prop))
		{
			FScriptMapHelper Helper(Map, ValuePtr);
			for (FScriptMapHelper::FIterator It(Helper); It; ++It)
			{
				FString KeyExport;
				Map->KeyProp->ExportTextItem_Direct(KeyExport, Helper.GetKeyPtr(It), nullptr, nullptr, PPF_None);
				RecurseInstanced(Map->ValueProp, Helper.GetValuePtr(It), OwnerKey, FString::Printf(TEXT("%s[%s]"), *PathPrefix, *Sanitize(KeyExport)), Bundle);
			}
			return;
		}
		// Struct: descend into its instanced-bearing fields, extending the path with the field name. Non-instanced sibling
		// fields are dropped (acceptable — the only corpus case is FQuestRewardSet, whose only field IS the instanced array).
		if (const FStructProperty* Struct = CastField<FStructProperty>(Prop))
		{
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				if (!IsInstancedBearing(*It))
				{
					continue;
				}
				RecurseInstanced(*It, It->ContainerPtrToValuePtr<void>(ValuePtr), OwnerKey, FString::Printf(TEXT("%s.%s"), *PathPrefix, *It->GetName()), Bundle);
			}
		}
	}

	// Serialize Entity into its type table (creating the table + capturing the column list on first encounter of the class)
	// and recurse instanced-bearing properties into child rows. Graph nodes and instanced sub-objects share this one path —
	// only the key differs. ExtraCells injects structural columns (e.g. "graph") that aren't reflected properties; per-class
	// column consistency holds because node rows always pass the same shape, sub-object rows always pass none, and no class
	// appears as both.
	void CollectEntityRow(const UObject* Entity, const FString& Key, const TMap<FString, FString>& ExtraCells, FExportBundle& Bundle)
	{
		const UClass* Class = Entity->GetClass();
		FExportTable& Table = Bundle.TablesByType.FindOrAdd(TypeStem(Class));

		// First row of this type: capture columns from class reflection — "class" leads (a row stays self-describing when
		// copied out of its file), then injected structural columns, then EditAnywhere non-Transient properties in
		// reflection order. Every column always written; instanced-bearing properties are child rows, not columns.
		if (Table.Columns.IsEmpty())
		{
			Table.Columns.Add(TEXT("class"));
			for (const TPair<FString, FString>& Extra : ExtraCells)
			{
				Table.Columns.Add(Extra.Key);
			}
			for (TFieldIterator<FProperty> It(Class); It; ++It)
			{
				// CPF_EditConst excludes VisibleAnywhere properties (inspectable, not authorable — e.g. QuestGuid,
				// which is already the row key). The authored-config line is "designer can EDIT it".
				if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst) || IsInstancedBearing(*It))
				{
					continue;
				}
				Table.Columns.Add(It->GetName());
			}
		}

		FExportRow Row;
		Row.Key = Key;
		Row.Cells.Add(TEXT("class"), Class->GetName());
		Row.Cells.Append(ExtraCells);
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst))
			{
				continue;
			}
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Entity);
			if (IsInstancedBearing(Prop))
			{
				RecurseInstanced(Prop, ValuePtr, Key, Prop->GetName(), Bundle);
				continue;
			}
			Row.Cells.Add(Prop->GetName(), Sanitize(SerializeCell(Prop, ValuePtr)));
		}
		Table.Rows.Add(MoveTemp(Row));
	}

	// Wire-edge verb by source pin category. Every edge is written output→input (signal-flow-forward) and the verbs read
	// true in that direction — "FactTag feeds-prereq Chapter_1" means the fact leaf feeds the gate's prereq input.
	FString EdgeVerb(FName PinCategory)
	{
		if (PinCategory == TEXT("QuestActivation"))   return TEXT("activates");
		if (PinCategory == TEXT("QuestOutcome"))      return TEXT("outcome");
		if (PinCategory == TEXT("QuestPrerequisite")) return TEXT("feeds-prereq");
		// Output-side category is past-tense "QuestDeactivated" (the input side's "QuestDeactivate" never appears
		// as an edge source — sources are always output pins).
		if (PinCategory == TEXT("QuestDeactivated"))   return TEXT("deactivates");
		return FString::Printf(TEXT("wire:%s"), *PinCategory.ToString());
	}

	// Emit knot-collapsed wire edges for one node: every output pin's terminals via the traversal policy's forward walk
	// (works for any output pin — the zero-knot case degenerates to the direct link). Fresh Visited per source pin: the
	// walker's visited set is node-granular, so sharing one across pins would suppress legitimate edges from later pins.
	void CollectEdgesForNode(const UQuestlineNodeBase* Node, const FQuestlineGraphTraversalPolicy& Policy, FExportBundle& Bundle)
	{
		const FString FromKey = NodeKeyOf(Node);
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}
			TArray<const UEdGraphPin*> Terminals;
			TSet<const UEdGraphNode*> Visited;
			Policy.CollectDownstreamTerminalInputs(Pin, Terminals, Visited);
			for (const UEdGraphPin* Terminal : Terminals)
			{
				const UQuestlineNodeBase* ToNode = Cast<UQuestlineNodeBase>(Terminal->GetOwningNode());
				if (!ToNode)
				{
					continue;   // non-questline node downstream — shouldn't occur; skip defensively
				}
				const FString Type = FString::Printf(TEXT("%s(%s)"), *EdgeVerb(Pin->PinType.PinCategory), *Pin->PinName.ToString());
				Bundle.Edges.Add({ FromKey, Type, NodeKeyOf(ToNode) });
			}
		}
	}

	// Recursively collect one graph level: entity rows + wire edges for every non-knot questline node (content, utility,
	// portal, prereq alike), contains edges + recursion for Quest inner graphs. Knots get no rows and no outgoing edges —
	// they're collapsed into the wire walk. LinkedQuestline placements are NOT recursed: the LinkedGraph soft-path column
	// on their own row is the cross-folder FK. GraphCell = "root" at top level, else the owning Quest container's key.
	void CollectGraph(const UEdGraph* Graph, const FString& GraphCell, const FQuestlineGraphTraversalPolicy& Policy, FExportBundle& Bundle)
	{
		if (!Graph)
		{
			return;
		}
		for (const UEdGraphNode* RawNode : Graph->Nodes)
		{
			const UQuestlineNodeBase* Node = Cast<UQuestlineNodeBase>(RawNode);
			if (!Node)
			{
				continue;   // comment bubbles and other non-questline graph furniture
			}
			if (Node->IsPassThroughNode())
			{
				++Bundle.KnotsCollapsed;
				continue;
			}

			const FString Key = NodeKeyOf(Node);
			TMap<FString, FString> Extra;
			Extra.Add(TEXT("graph"), GraphCell);
			CollectEntityRow(Node, Key, Extra, Bundle);
			CollectEdgesForNode(Node, Policy, Bundle);

			// Quest container: contains edge to each inner node, then recurse. Emitted here (not inside the recursion)
			// so the edge's from-side is unambiguous.
			if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
			{
				if (const UEdGraph* Inner = QuestNode->GetInnerGraph())
				{
					for (const UEdGraphNode* InnerRaw : Inner->Nodes)
					{
						const UQuestlineNodeBase* InnerNode = Cast<UQuestlineNodeBase>(InnerRaw);
						if (!InnerNode || InnerNode->IsPassThroughNode())
						{
							continue;
						}
						Bundle.Edges.Add({ Key, TEXT("contains(InnerGraph)"), NodeKeyOf(InnerNode) });
					}
					CollectGraph(Inner, Key, Policy, Bundle);
				}
			}
		}
	}

	// Deterministic serialization: per-type files (sorted stems), rows sorted by key, header row = "key" + the type's
	// columns; edges sorted (from, type, to). Two exports of an unchanged asset are byte-identical. Returns false on any
	// file-write failure. Duplicate edge rows (two real parallel wires to one destination) are written as-is — honest topology.
	bool WriteBundle(FExportBundle& Bundle, const FString& OutDir, int32& OutRowTotal)
	{
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
		OutRowTotal = 0;

		TArray<FString> Stems;
		Bundle.TablesByType.GetKeys(Stems);
		Stems.Sort();
		for (const FString& Stem : Stems)
		{
			FExportTable& Table = Bundle.TablesByType[Stem];
			Table.Rows.Sort([](const FExportRow& A, const FExportRow& B) { return A.Key < B.Key; });

			TArray<FString> Lines;
			Lines.Add(FString::Printf(TEXT("key\t%s"), *FString::Join(Table.Columns, TEXT("\t"))));
			for (const FExportRow& Row : Table.Rows)
			{
				TArray<FString> Cells;
				Cells.Add(Row.Key);
				for (const FString& Col : Table.Columns)
				{
					const FString* Cell = Row.Cells.Find(Col);
					Cells.Add(Cell ? *Cell : FString());
				}
#if !UE_BUILD_SHIPPING
				// Drift guard: a cell not in Columns means per-class column capture diverged from a row's actual cells.
				for (const TPair<FString, FString>& CellPair : Row.Cells)
				{
					ensureMsgf(Table.Columns.Contains(CellPair.Key),
						TEXT("ExportQuestline: row '%s' carries cell '%s' absent from '%s' columns."), *Row.Key, *CellPair.Key, *Stem);
				}
#endif
				Lines.Add(FString::Join(Cells, TEXT("\t")));
			}
			OutRowTotal += Table.Rows.Num();

			const FString Path = OutDir / (Stem + TEXT(".tsv"));
			if (!FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *Path))
			{
				UE_LOG(LogSimpleQuest, Warning, TEXT("ExportQuestline: failed to write '%s'."), *Path);
				return false;
			}
			UE_LOG(LogSimpleQuest, Verbose, TEXT("ExportQuestline: wrote '%s' (%d row(s))."), *Path, Table.Rows.Num());
		}

		Bundle.Edges.Sort([](const FExportEdge& A, const FExportEdge& B)
		{
			if (A.From != B.From) return A.From < B.From;
			if (A.Type != B.Type) return A.Type < B.Type;
			return A.To < B.To;
		});
		TArray<FString> EdgeLines;
		EdgeLines.Add(TEXT("from\ttype\tto"));
		for (const FExportEdge& E : Bundle.Edges)
		{
			EdgeLines.Add(FString::Printf(TEXT("%s\t%s\t%s"), *E.From, *E.Type, *E.To));
		}
		const FString EdgePath = OutDir / TEXT("edges.tsv");
		if (!FFileHelper::SaveStringToFile(FString::Join(EdgeLines, TEXT("\n")), *EdgePath))
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportQuestline: failed to write '%s'."), *EdgePath);
			return false;
		}
		UE_LOG(LogSimpleQuest, Verbose, TEXT("ExportQuestline: wrote '%s' (%d edge(s))."), *EdgePath, Bundle.Edges.Num());
		return true;
	}

	void ExportQuestlineCmd(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportQuestline: usage 'SimpleQuest.ExportQuestline <QuestlineAssetPath>'."));
			return;
		}
		const UQuestlineGraph* Graph = LoadObject<UQuestlineGraph>(nullptr, *Args[0]);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("ExportQuestline: couldn't load questline asset or its authored graph '%s'."), *Args[0]);
			return;
		}

		FExportBundle Bundle;
		const TUniquePtr<FQuestlineGraphTraversalPolicy> Policy = MakeUnique<FQuestlineGraphTraversalPolicy>();

		// Questline-self row: the asset's own authored fields (QuestlineID / DisplayName / Description / DisplayData /
		// ResettableReplay as columns; QuestlineRewards explodes through the instanced recursion into reward child rows).
		// Keyed by the SANITIZED EffectiveID — the same segment form compiled tags use, so the export key aligns with
		// tag identity and stays interchange-safe (no spaces/punctuation in keys or folder names).
		const FString SelfKey = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Graph->GetEffectiveID());
		CollectEntityRow(Graph, SelfKey, {}, Bundle);

		CollectGraph(Graph->QuestlineEdGraph, TEXT("root"), *Policy, Bundle);

		const FString OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport") / SelfKey);
		int32 RowTotal = 0;
		if (WriteBundle(Bundle, OutDir, RowTotal))
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("ExportQuestline: '%s' — %d entity row(s) across %d type(s), %d edge(s), %d knot(s) collapsed. Wrote '%s'."),
				*SelfKey, RowTotal, Bundle.TablesByType.Num(), Bundle.Edges.Num(), Bundle.KnotsCollapsed, *OutDir);
		}
	}
}

static FAutoConsoleCommand GExportQuestlineCmd(
	TEXT("SimpleQuest.ExportQuestline"),
	TEXT("PROTOTYPE: export a questline's authored model as the interlingua folder — per-type entity tables "
		"(reflection-driven, instanced sub-objects as child rows) + one knot-collapsed edge table — to "
		"Saved/QuestExport/<QuestlineID>/. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportQuestlineCmd));
