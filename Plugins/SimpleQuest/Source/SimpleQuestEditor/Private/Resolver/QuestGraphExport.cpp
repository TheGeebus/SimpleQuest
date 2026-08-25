// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestGraphExport.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestDataValueBuilder.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Resolver/QuestReflectionUtils.h"
#include "UObject/UnrealType.h"


/**
 * Table file stem for a class or script struct: strip the QuestlineNode_ prefix, snake_case the remainder. Underscores
 * insert only on a lower→upper boundary so acronym runs stay together. Derived display, not identity - collisions can't
 * occur because class names are unique and the transform is injective enough for this corpus.
 */
static FString TypeStem(const UStruct* Type)
{
	FString Name = Type->GetName();
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

/** Node row key - QuestGuid digits. Every UQuestlineNodeBase carries QuestGuid (base-class field), so no fallback needed. */
static FString NodeKeyOf(const UQuestlineNodeBase* Node)
{
	return Node->QuestGuid.ToString(EGuidFormats::Digits);
}

/**
 * Emit child rows + contains edges for every instanced object reachable from Prop on the entity keyed OwnerKey.
 * PathPrefix is the property path so far relative to OwnerKey (e.g. "Rewards" or "QuestlineRewards[<key>].Rewards");
 * it becomes both the contains-edge qualifier and the child row's synthetic key suffix, so edge and key corroborate.
 */
static void RecurseInstanced(const FProperty* Prop, const void* ValuePtr, const FString& OwnerKey, const FString& PathPrefix, FQuestDataBundle& Bundle)
{
	ForEachQuestInstancedChild(Prop, ValuePtr, OwnerKey, PathPrefix,
		[&Bundle, &OwnerKey](const FString& ChildKey, const FString& Path, const FQuestInstancedChild& Child, int32 ArrayOrdinal)
		{
			Bundle.Edges.Add({ OwnerKey, FString::Printf(TEXT("contains(%s)"), *Path), ChildKey });

			TMap<FString, FString> Extra;
			Extra.Add(TEXT("index"), ArrayOrdinal == INDEX_NONE ? FString() : FString::FromInt(ArrayOrdinal));
			if (Child.IsStruct())
			{
				CollectQuestStructRow(Child.StructType, Child.Memory, ChildKey, Extra, Bundle);
			}
			else
			{
				CollectQuestEntityRow(Child.Object, ChildKey, Extra, Bundle);   // which recurses this child's own instanced properties
			}
		});
}

void CollectQuestEntityRow(const UObject* Entity, const FString& Key, const TMap<FString, FString>& ExtraCells, FQuestDataBundle& Bundle)
{
	const UClass* Class = Entity->GetClass();
	FQuestDataTable& Table = Bundle.TablesByType.FindOrAdd(TypeStem(Class));

	// First row of this type: capture columns from class reflection - "class" leads (a row stays self-describing when
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
			if (!IsAuthoredConfigProperty(*It) || IsQuestInstancedBearing(*It))
			{
				continue;
			}
			Table.Columns.Add(It->GetName());
		}
	}

	// CDO of the entity's class - the per-property default the Q6 rule compares against (BuildValue emits Empty when
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
		if (IsQuestInstancedBearing(Prop))
		{
			RecurseInstanced(Prop, ValuePtr, Key, Prop->GetName(), Bundle);
			continue;
		}
		const void* DefaultPtr = DefaultObject ? Prop->ContainerPtrToValuePtr<void>(DefaultObject) : nullptr;
		Row.Cells.Add(Prop->GetName(), BuildQuestDataValue(Prop, ValuePtr, DefaultPtr));
	}
	Table.Rows.Add(MoveTemp(Row));
}

/**
 * The struct sibling of CollectQuestEntityRow. Same row shape, same column-capture rule, same default-elision - the only
 * differences are that the type cell is named "struct" rather than "class", and that the defaults come from a
 * zero-initialized instance of the script struct rather than a CDO.
 *
 * A SEPARATE CELL FROM "class" ON PURPOSE: the two resolve through different lookups, and one cell serving both would
 * let a malformed row resolve as the wrong kind of thing rather than refusing.
 */
void CollectQuestStructRow(const UScriptStruct* Type, const void* Memory, const FString& Key, const TMap<FString, FString>& ExtraCells, FQuestDataBundle& Bundle)
{
	if (!Type || !Memory) return;

	FQuestDataTable& Table = Bundle.TablesByType.FindOrAdd(TypeStem(Type));

	if (Table.Columns.IsEmpty())
	{
		Table.Columns.Add(TEXT("struct"));
		for (const TPair<FString, FString>& Extra : ExtraCells)
		{
			Table.Columns.Add(Extra.Key);
		}
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			if (!IsAuthoredConfigProperty(*It) || IsQuestInstancedBearing(*It)) continue;
			Table.Columns.Add(It->GetName());
		}
	}

	// A zero-initialized instance is the struct's equivalent of a CDO - the per-property default BuildQuestDataValue
	// compares against so an unchanged field elides to Empty instead of restating the default in every row.
	TArray<uint8> DefaultMemory;
	DefaultMemory.SetNumZeroed(Type->GetStructureSize());
	Type->InitializeStruct(DefaultMemory.GetData());
	ON_SCOPE_EXIT { Type->DestroyStruct(DefaultMemory.GetData()); };

	FQuestDataRow Row;
	Row.Key = Key;
	{
		FQuestDataValue TypeCell;
		TypeCell.Kind = EQuestDataValueKind::String;
		TypeCell.StringForm = Type->GetName();
		Row.Cells.Add(TEXT("struct"), TypeCell);
	}
	for (const TPair<FString, FString>& Extra : ExtraCells)
	{
		FQuestDataValue ExtraCell;
		ExtraCell.Kind = EQuestDataValueKind::String;
		ExtraCell.StringForm = Extra.Value;
		Row.Cells.Add(Extra.Key, ExtraCell);
	}
	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		const FProperty* Prop = *It;
		if (!IsAuthoredConfigProperty(Prop)) continue;

		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Memory);
		if (IsQuestInstancedBearing(Prop))
		{
			RecurseInstanced(Prop, ValuePtr, Key, Prop->GetName(), Bundle);   // a payload can itself nest
			continue;
		}
		Row.Cells.Add(Prop->GetName(), BuildQuestDataValue(Prop, ValuePtr, Prop->ContainerPtrToValuePtr<void>(DefaultMemory.GetData())));
	}
	Table.Rows.Add(MoveTemp(Row));
}

void CollectQuestGraphBundle(const UEdGraph* Graph, const FString& GraphCell, const FQuestlineGraphTraversalPolicy& Policy, FQuestDataBundle& Bundle)
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
		CollectQuestEntityRow(Node, Key, Extra, Bundle);
		CollectQuestWireEdges(Node, Policy, Bundle.Edges);

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
				CollectQuestGraphBundle(Inner, Key, Policy, Bundle);
			}
		}
	}
}

