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
#include "QuestDataValue.h"
#include "Quests/QuestlineGraph.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestReflectionUtils.h"
#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/QuestMappingSource.h"
#include "Settings/SimpleQuestSettings.h"
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

	// Build the structured neutral cell for one non-instanced property value. The type-dispatch that used to live in
	// SerializeCell moves here — the type-dispatch produces a structured value; providers render the string form on write.
	// DefaultPtr is the same property on the class CDO — when the live value equals it (FProperty::Identical), the cell
	// is Empty (Q6: import skips, leaving the constructed default). Cast-ladder order is load-bearing where noted. Container
	// elements recurse with a null default (no per-slot CDO — presence is the authored fact).
	FQuestDataValue BuildValue(const FProperty* Prop, const void* ValuePtr, const void* DefaultPtr)
	{
		if (!Prop || !ValuePtr)
		{
			return FQuestDataValue::MakeEmpty();
		}

		// Q6 — Empty iff live value == CDO-constructed default. DefaultPtr MUST be non-null (a null default makes
		// Identical treat every struct prop as different). Applies to leaves and containers alike.
		if (DefaultPtr && Prop->Identical(ValuePtr, DefaultPtr, PPF_None))
		{
			return FQuestDataValue::MakeEmpty();
		}

		FQuestDataValue V;

		// Enum BEFORE numeric/byte. SimpleQuest's UENUM(uint8) enum-class props are FEnumProperty; the FByteProperty-
		// with-Enum branch only fires for TEnumAsByte / old namespaced enums. Both precede Scalar.
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const FNumericProperty* Under = EnumProp->GetUnderlyingProperty();
			V.EnumValue = Under->GetSignedIntPropertyValue(ValuePtr);
			V.Kind = EQuestDataValueKind::Enum;
			V.StringForm = EnumProp->GetEnum()->GetNameStringByValue(V.EnumValue);
			return V;
		}
		if (const FByteProperty* ByteEnum = CastField<FByteProperty>(Prop); ByteEnum && ByteEnum->Enum)
		{
			V.EnumValue = ByteEnum->GetPropertyValue(ValuePtr);
			V.Kind = EQuestDataValueKind::Enum;
			V.StringForm = ByteEnum->Enum->GetNameStringByValue(V.EnumValue);
			return V;
		}

		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Bool;
			V.bBool = BoolProp->GetPropertyValue(ValuePtr);
			return V;
		}

		if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Text;
			V.Text = TextProp->GetPropertyValue(ValuePtr);   // a real FText (loc namespace/key preserved)
			return V;
		}

		// Struct sub-cases BEFORE the generic FStructProperty -> StructLiteral fallback.
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FGameplayTag>::Get())
			{
				V.Kind = EQuestDataValueKind::Tag;
				V.Tag = *static_cast<const FGameplayTag*>(ValuePtr);
				return V;
			}
			if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
			{
				V.Kind = EQuestDataValueKind::TagContainer;
				V.TagContainer = *static_cast<const FGameplayTagContainer*>(ValuePtr);
				return V;
			}
			// Any other struct (incl. FInstancedStruct) -> opaque typed literal (Scalar holds the ExportTextItem string).
			// Guard-LOG (not STOP): surface the opacity once per (struct,prop) so a new adopter struct / bare-struct field
			// is VISIBLE, not silently opaque. Round-trips today via ImportText.
			static TSet<FString> GLoggedStructFallbacks;
			const FString StructKey = FString::Printf(TEXT("%s::%s"), *StructProp->Struct->GetName(), *Prop->GetName());
			if (!GLoggedStructFallbacks.Contains(StructKey))
			{
				GLoggedStructFallbacks.Add(StructKey);
				UE_LOG(LogSimpleQuest, Warning, TEXT("ExportQuestline: StructLiteral fallback for %s (struct '%s') — opaque "
					"to structured providers; structure-me-later candidate."), *StructKey, *StructProp->Struct->GetName());
			}
			V.Kind = EQuestDataValueKind::StructLiteral;
			Prop->ExportTextItem_Direct(V.StringForm, ValuePtr, nullptr, nullptr, PPF_None);
			return V;
		}

		// Soft object OR soft class (FSoftClassProperty : FSoftObjectProperty — the base cast covers both).
		if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Reference;
			V.StringForm = static_cast<const FSoftObjectPtr*>(ValuePtr)->ToString();
			return V;
		}

		// Array of the above.
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Array;
			FScriptArrayHelper Helper(ArrayProp, ValuePtr);
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				V.Elements.Add(BuildValue(ArrayProp->Inner, Helper.GetRawPtr(i), nullptr));
			}
			return V;
		}

		// Set of the above (sparse storage — skip invalid indices, stop once Num() live elements seen).
		if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			V.Kind = EQuestDataValueKind::Array;
			FScriptSetHelper Helper(SetProp, ValuePtr);
			for (int32 i = 0, Seen = 0; Seen < Helper.Num(); ++i)
			{
				if (!Helper.IsValidIndex(i)) continue;
				++Seen;
				V.Elements.Add(BuildValue(SetProp->ElementProp, Helper.GetElementPtr(i), nullptr));
			}
			return V;
		}

		// Leaf value — classify Number vs String by the FProperty type (honest, from reflection; no guessing). Numeric =
		// any FNumericProperty (int/int64/float/double/raw-uint8; byte-with-enum already handled above). String = FStr/
		// FName. Both carry their string form in StringForm; the Number/String split lets a structured provider (JSON)
		// render a number natively while TSV renders both as text. Anything else (defensive) falls to String.
		V.Kind = (CastField<FNumericProperty>(Prop) != nullptr) ? EQuestDataValueKind::Number : EQuestDataValueKind::String;
		Prop->ExportTextItem_Direct(V.StringForm, ValuePtr, nullptr, nullptr, PPF_None);
		return V;
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
			Row.Cells.Add(Prop->GetName(), BuildValue(Prop, ValuePtr, DefaultPtr));
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
		CollectEntityRow(Graph, SelfKey, {}, Bundle);

		CollectGraph(Graph->QuestlineEdGraph, TEXT("root"), *Policy, Bundle);

		const FString OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestExport") / SelfKey);

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
