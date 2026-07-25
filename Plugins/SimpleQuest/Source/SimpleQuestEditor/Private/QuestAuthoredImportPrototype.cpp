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
#include "EdGraphNode_Comment.h"
#include "Misc/Paths.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/TsvQuestDataFormat.h"
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
#include "Resolver/JsonQuestDataFormat.h"
#include "Utilities/QuestlineGraphCompiler.h"
#include "Utilities/SimpleQuestEditorUtils.h"

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

	// Pick the format provider from an optional "--format=<name>" console arg (default TSV). The arg is what the ORACLE
	// needs to pin a format deterministically; a project setting/registry is the later human-facing selection seam.
	TUniquePtr<ISimpleQuestDataFormat> MakeQuestDataFormat(const TArray<FString>& Args)
	{
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--format=")))
			{
				const FString Name = Arg.RightChop(9);
				if (Name.Equals(TEXT("json"), ESearchCase::IgnoreCase)) return MakeUnique<FJsonQuestDataFormat>();
			}
		}
		return MakeUnique<FTsvQuestDataFormat>();   // default
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
					const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Elem.Scalar), false);
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
	// Enum/Reference/StructLiteral) go through ImportText from Value.Scalar. The TSV provider produces Kind=Scalar for
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

		case EQuestDataValueKind::Scalar:
		case EQuestDataValueKind::Enum:
		case EQuestDataValueKind::Reference:
		case EQuestDataValueKind::StructLiteral:
		default:
		{
			// String-carrying Kinds. Value.Scalar holds the cell string (== today's CellText for the TSV path). The FText
			// special-case is PRESERVED here because TSV delivers FText cells as Kind=Scalar (a Scalar holding NSLOCTEXT);
			// routing them to ImportText instead of ReadFromBuffer would regress TSV. JSON never lands here for FText (it
			// uses Kind=Text above), so this branch only ever sees TSV's FText-as-Scalar — exactly the old behavior.
			if (Value.Scalar.IsEmpty()) return;
			if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
			{
				FText Parsed;
				const TCHAR* Buffer = *Value.Scalar;
				if (FTextStringHelper::ReadFromBuffer(Buffer, Parsed))
				{
					TextProp->SetPropertyValue(ValuePtr, Parsed);
				}
				return;
			}
			Prop->ImportText_Direct(*Value.Scalar, ValuePtr, /*OwnerObject*/ nullptr, PPF_None);
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
			// RestoreCell types the string against the property (blind-parse contract §1); the value's CanonicalText is
			// exactly the cell string the format produced, so this is byte-identical to the pre-Stage-2 string path.
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

		// P0 — read the folder via the format provider (picked from --format, default TSV), then validate the neutral
		// bundle structurally, before creating anything. Provider reads files -> bundle; ValidateBundle checks the bundle
		// (provider-agnostic). MakeQuestDataFormat scans the ORIGINAL Args for --format (present or not among PathArgs).
		const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args);
		FQuestDataBundle Bundle;
		if (!Format->ReadBundle(FolderPath, Bundle))
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("ImportQuestline: could not read '%s' as %s. No asset created."), *FolderPath, *Format->FormatName());
			return;
		}
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

		TArray<FString> Warnings;
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

		// P5 — compile + save. A brand-new asset needs TWO compile passes to reach steady state, exactly as it would
		// through normal authoring (create -> compile registers the asset's identity + state tags -> recompile sees
		// them valid). The compiler captures CurrentAssetIdentityTag via RequestGameplayTag(..., ErrorIfNotFound=false)
		// at the top of Compile(), so on the FIRST compile of a never-registered identity that tag is invalid and the
		// asset-root-scope graph-resolution record (NextNodesByPath for terminating outcomes) is skipped. The first
		// pass registers the tags; the second sees a valid identity and produces the complete compiled model.
		TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
		Compiler->Compile(Graph);                          // pass 1: registers the identity + state tags
		const bool bCompiled = Compiler->Compile(Graph);   // pass 2: identity now valid -> complete resolution records

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