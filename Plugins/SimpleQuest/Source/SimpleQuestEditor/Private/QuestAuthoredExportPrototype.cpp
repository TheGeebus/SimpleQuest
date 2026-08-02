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
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "SimpleQuestLog.h"
#include "Resolver/QuestDataValue.h"
#include "Quests/QuestlineGraph.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestReflectionUtils.h"
#include "Resolver/QuestDataValueBuilder.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestMappingSource.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	// Make an exported map-key safe to embed inside a neutral ROW KEY (e.g. "QuestlineRewards[<key>].Rewards"): a key
	// with an embedded tab/newline would corrupt the path segment the import later splits on. This is a KEY-well-formed-
	// ness concern of the neutral bundle, NOT format escaping — it stays in the routing core regardless of provider.
	// (Distinct from the provider's own cell-escaping, which happens to use the same three replacements today.)
	FString SanitizeKeySegment(const FString& In)
	{
		return In.Replace(TEXT("\t"), TEXT("\\t")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("\\n"));
	}

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

	void CollectEntityRow(const UObject* Entity, const FString& Key, const TMap<FString, FString>& ExtraCells, FQuestDataBundle& Bundle);

	// Emit child rows + contains edges for every instanced object reachable from Prop on the entity keyed OwnerKey.
	// PathPrefix is the property path so far relative to OwnerKey (e.g. "Rewards" or "QuestlineRewards[<key>].Rewards");
	// it becomes both the contains-edge qualifier and the child row's synthetic key suffix, so edge and key corroborate.
	void RecurseInstanced(const FProperty* Prop, const void* ValuePtr, const FString& OwnerKey, const FString& PathPrefix, FQuestDataBundle& Bundle)
	{
		// Direct instanced object: one child row. Null slot = no row, no edge — absence is the honest representation.
		if (const FObjectProperty* Obj = CastField<FObjectProperty>(Prop))
		{
			if (const UObject* Sub = Obj->GetObjectPropertyValue(ValuePtr))
			{
				const FString ChildKey = FString::Printf(TEXT("%s/%s"), *OwnerKey, *PathPrefix);
				Bundle.Edges.Add({ OwnerKey, FString::Printf(TEXT("contains(%s)"), *PathPrefix), ChildKey });
				CollectEntityRow(Sub, ChildKey, {}, Bundle);
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
				RecurseInstanced(Map->ValueProp, Helper.GetValuePtr(It), OwnerKey, FString::Printf(TEXT("%s[%s]"), *PathPrefix, *SanitizeKeySegment(KeyExport)), Bundle);
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
	void CollectEntityRow(const UObject* Entity, const FString& Key, const TMap<FString, FString>& ExtraCells, FQuestDataBundle& Bundle)
	{
		const UClass* Class = Entity->GetClass();
		FQuestDataTable& Table = Bundle.TablesByType.FindOrAdd(TypeStem(Class));

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
				if (!IsAuthoredConfigProperty(*It) || IsInstancedBearing(*It))
				{
					continue;
				}
				Table.Columns.Add(It->GetName());
			}
		}

		// CDO of the entity's class — the per-property default the Q6 rule compares against (BuildValue emits Empty when
		// the live value equals it). GetDefaultObject(true) guarantees a non-null CDO (a null default would make
		// FProperty::Identical treat every struct prop as different).
		const UObject* DefaultObject = Class->GetDefaultObject(/*bCreateIfNeeded*/ true);

		FQuestDataRow Row;
		Row.Key = Key;
		{
			FQuestDataValue ClassCell;
			ClassCell.Kind = EQuestDataValueKind::String;
			ClassCell.StringForm = Class->GetName();
			Row.Cells.Add(TEXT("class"), ClassCell);
		}
		for (const TPair<FString, FString>& Extra : ExtraCells)
		{
			FQuestDataValue ExtraCell;
			ExtraCell.Kind = EQuestDataValueKind::String;
			ExtraCell.StringForm = Extra.Value;
			Row.Cells.Add(Extra.Key, ExtraCell);
		}
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!IsAuthoredConfigProperty(Prop))
			{
				continue;
			}
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Entity);
			if (IsInstancedBearing(Prop))
			{
				RecurseInstanced(Prop, ValuePtr, Key, Prop->GetName(), Bundle);
				continue;
			}
			const void* DefaultPtr = DefaultObject ? Prop->ContainerPtrToValuePtr<void>(DefaultObject) : nullptr;
			Row.Cells.Add(Prop->GetName(), BuildQuestDataValue(Prop, ValuePtr, DefaultPtr));
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
	void CollectEdgesForNode(const UQuestlineNodeBase* Node, const FQuestlineGraphTraversalPolicy& Policy, FQuestDataBundle& Bundle)
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
	void CollectGraph(const UEdGraph* Graph, const FString& GraphCell, const FQuestlineGraphTraversalPolicy& Policy, FQuestDataBundle& Bundle)
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

	// Map exported row key (QuestGuid digits) -> the studio's own key, and -> the node itself. The node is needed to resolve
	// which output pin an edge left from, so an unqualified wire binding can be matched back the same way the import
	// resolves it forward. Walks the graph directly: we only need identity, so visiting a node the export skips is harmless.
	void CollectNodeIdentity(const UEdGraph* EdGraph, TMap<FString, FString>& OutSourceKeyByGuid,
	                         TMap<FString, const UQuestlineNodeBase*>& OutNodeByGuid)
	{
		if (!EdGraph) return;
		for (const UEdGraphNode* RawNode : EdGraph->Nodes)
		{
			const UQuestlineNodeBase* Node = Cast<UQuestlineNodeBase>(RawNode);
			if (!Node) continue;
			const FString Guid = Node->QuestGuid.ToString(EGuidFormats::Digits);
			OutNodeByGuid.Add(Guid, Node);
			if (!Node->ImportSourceKey.IsEmpty())
			{
				OutSourceKeyByGuid.Add(Guid, Node->ImportSourceKey);
			}
			if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
			{
				CollectNodeIdentity(QuestNode->GetInnerGraph(), OutSourceKeyByGuid, OutNodeByGuid);
			}
		}
	}

	// Which wire binding, if any, does this edge belong to? Qualified bindings are tested FIRST so a named-outcome column is
	// never swallowed by the default-flow one. An unqualified binding then claims an edge only when its pin is the from-node's
	// DEFAULT output for that verb — the same rule the import applies forward, read backwards.
	const FQuestWireBinding* FindClaimingWireBinding(const FQuestDataEdge& Edge, const UQuestImportMapping& Mapping,
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
	
	// ---- REVERSE MAPPING (restate the canonical bundle in the STUDIO's vocabulary) ---------------------------------
	// The inverse read of the same recipe the import uses forward: our class -> their discriminator value, our property
	// names -> their column names, and the per-type tables merged back into the ONE mixed table their source actually was.
	// Runs on the bundle the exporter just built, immediately before serialization — no second graph walk.
	// This is a faithful RE-STATEMENT, not a regeneration: per-row casing, values equal to our defaults, and the studio's
	// original file arrangement are not recoverable from the graph.
	void ApplyReverseMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping,
							 const TMap<FString, FString>& SourceKeyByGuid,
							 const TMap<FString, const UQuestlineNodeBase*>& NodeByGuid, TArray<FString>& Warnings)
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
				const FString* MappedTo = SourceKeyByGuid.Find(Edge.To);
				WireCellsByRow.FindOrAdd(Edge.From)
							  .FindOrAdd(Claim->SourceColumn.ToString())
							  .Add(MappedTo ? *MappedTo : Edge.To);
				ClaimedEdges.Add(i);
				++WiresClaimed;
			}
		}

		int32 Restated = 0;
		for (TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
		{
			if (TablePair.Key == TEXT("questline_graph")) continue;   // the self row keeps its own shape

			for (FQuestDataRow& Row : TablePair.Value.Rows)
			{
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

				// C. Wiring this row declares, written as their column(s). MUST run before C — WireCellsByRow is keyed by the
				//     exported GUID, and C is about to replace Row.Key with the studio's key. A single target is written bare;
				//     several use the same paren list the import parses, so the value round-trips through ParseFlowKeyList.
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
				//    our GUIDs; restoring it is what makes the file readable as a diff against their source.
				if (const FString* SourceKey = SourceKeyByGuid.Find(Row.Key))
				{
					Row.Key = *SourceKey;
				}

				// E. Text as plain text. Import turned their plain string into an FText and INVENTED a localization key for
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

		// 5. Edge endpoints reference rows by key, so they must follow the same substitution — otherwise the wiring points at
		//    GUIDs that no longer appear in any row.
		for (FQuestDataEdge& Edge : Bundle.Edges)
		{
			if (const FString* From = SourceKeyByGuid.Find(Edge.From)) { Edge.From = *From; }
			if (const FString* To   = SourceKeyByGuid.Find(Edge.To))   { Edge.To   = *To; }
		}

		UE_LOG(LogSimpleQuest, Log, TEXT("ExportQuestline: restated %d row(s) in the recipe's vocabulary, %d column(s) kept of %d "
			"(faithful re-statement — per-row casing, at-default values and original file layout are not reconstructed), %d wire(s) written."),
			Restated,
			ColsAfter,
			ColsBefore,
			WiresWritten);
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

		FQuestDataBundle Bundle;
		const TUniquePtr<FQuestlineGraphTraversalPolicy> Policy = MakeUnique<FQuestlineGraphTraversalPolicy>();

		// Questline-self row: the asset's own authored fields (QuestlineID / DisplayName / Description / DisplayData /
		// ResettableReplay as columns; QuestlineRewards explodes through the instanced recursion into reward child rows).
		// Keyed by the SANITIZED EffectiveID — the same segment form compiled tags use, so the export key aligns with
		// tag identity and stays interchange-safe (no spaces/punctuation in keys or folder names).
		const FString SelfKey = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Graph->GetEffectiveID());
		
		// The key can come out EMPTY from input a designer can type: a whitespace-only QuestlineID is not IsEmpty(), so the
		// asset-name fallback never fires, and the sanitizer trims it to nothing. An empty segment appends only a separator,
		// so the destination would collapse to the export ROOT and scatter this export across every other questline's output.
		// Refuse rather than write somewhere unintended, and name the field to fix.
		if (SelfKey.IsEmpty())
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("ExportQuestline: '%s' has a QuestlineID that reduces to an empty export key "
				"(raw value: '%s'). Give it at least one letter, digit or underscore — or clear the field entirely to fall back "
				"to the asset name. Nothing exported."), *Args[0], *Graph->GetEffectiveID());
			return;
		}
		CollectEntityRow(Graph, SelfKey, {}, Bundle);

		CollectGraph(Graph->QuestlineEdGraph, TEXT("root"), *Policy, Bundle);

		// Optional studio-shape restatement. Absent = canonical export (our vocabulary), byte-identical to before.
		TArray<FString> Warnings;
		if (const UQuestImportMapping* Mapping = LoadQuestMappingArg(Args))
		{
			TMap<FString, FString> SourceKeyByGuid;
			TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
			CollectNodeIdentity(Graph->QuestlineEdGraph, SourceKeyByGuid, NodeByGuid);
			ApplyReverseMapping(Bundle, *Mapping, SourceKeyByGuid, NodeByGuid, Warnings);
		}
		for (const FString& W : Warnings) { UE_LOG(LogSimpleQuest, Warning, TEXT("ExportQuestline: %s"), *W); }
		
		// Prove containment structurally instead of trusting the string that produced it — the destination must be exactly one
		// level below the export root. Holds even if the key derivation changes or is later fed from somewhere new.
		const FString ExportRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport"));
		const FString OutDir = FPaths::ConvertRelativePathToFull(ExportRoot / SelfKey);
		{
			FString NormRoot = ExportRoot;  FPaths::NormalizeDirectoryName(NormRoot);
			FString NormOut  = OutDir;      FPaths::NormalizeDirectoryName(NormOut);
			if (NormOut == NormRoot || FPaths::GetPath(NormOut) != NormRoot)
			{
				UE_LOG(LogSimpleQuest, Error, TEXT("ExportQuestline: refusing — destination '%s' is not a direct child of the "
					"export root '%s' (export key '%s'). Nothing exported."), *NormOut, *NormRoot, *SelfKey);
				return;
			}
		}
		UE_LOG(LogSimpleQuest, Log, TEXT("ExportQuestline: destination '%s'."), *OutDir);

		const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args, TEXT("ExportQuestline"));
		if (!Format)
		{
			return;   // the unregistered-format error was already logged; nothing exported.
		}
		if (Format->WriteBundle(Bundle, OutDir))
		{
			int32 RowTotal = 0;
			for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
			{
				RowTotal += TablePair.Value.Rows.Num();
			}
			UE_LOG(LogSimpleQuest, Log, TEXT("ExportQuestline: '%s' — %d entity row(s) across %d type(s), %d edge(s), %d knot(s) collapsed. Wrote '%s'."),
				*SelfKey,
				RowTotal,
				Bundle.TablesByType.Num(),
				Bundle.Edges.Num(),
				Bundle.KnotsCollapsed,
				*OutDir);
		}
	}
}

static FAutoConsoleCommand GExportQuestlineCmd(
	TEXT("SimpleQuest.ExportQuestline"),
	TEXT("PROTOTYPE: export a questline's authored model as the interlingua folder — per-type entity tables "
		"(reflection-driven, instanced sub-objects as child rows) + one knot-collapsed edge table — to "
		"Saved/QuestExport/<QuestlineID>/. Arg: the questline asset path."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportQuestlineCmd));
