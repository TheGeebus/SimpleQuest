// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestGraphBuilder.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestInstancedChildren.h"
#include "Resolver/QuestRowRestore.h"
#include "SimpleQuestLog.h"
#include "Utilities/SimpleQuestEditorUtils.h"


UEdGraphNode* SpawnQuestNodeFromRow(UEdGraph* TargetGraph, const FQuestDataRow& Row, const FQuestDataBundle& Bundle,
                               TMap<FString, UEdGraphNode*>& NodeByKey, TSet<FString>& Consumed, TArray<FString>& Warnings)
{
	const FString ClassName = Row.Get(TEXT("class"));
	UClass* Class = ResolveQuestBundleClass(ClassName);
	if (!Class) { Warnings.Add(FString::Printf(TEXT("unknown node class '%s' for key '%s'"), *ClassName, *Row.Key)); return nullptr; }

	UEdGraphNode* Node = NewObject<UEdGraphNode>(TargetGraph, Class, NAME_None, RF_Transactional);
	TargetGraph->AddNode(Node, false, false);
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
	RestoreQuestRowProperties(Node, Row);

	TSet<FString> LocalConsumed;
	ReattachQuestInstancedChildren(Node, Row.Key, Bundle, LocalConsumed, Warnings);
	Consumed.Append(LocalConsumed);

	NodeByKey.Add(Row.Key, Node);
	return Node;
}

/**
 * Remove schema-seeded default nodes (the auto-Entry) from a freshly-created graph so it's populated purely from
 * exported rows. Safe: CreateInnerGraph / the factory only use the default Entry as a starting affordance - nothing
 * retains it; the graph's Entry is always re-found by class (verified: CreateInnerGraph holds no ref, callers scan
 * Graph->Nodes for the Entry type). Schema + (for inner graphs) the change subscription are unaffected.
 */
static void ClearDefaultNodes(UEdGraph* Graph)
{
	if (!Graph) return;
	TArray<UEdGraphNode*> ToRemove = Graph->Nodes;   // copy - RemoveNode mutates the array
	for (UEdGraphNode* N : ToRemove)
	{
		if (N) Graph->RemoveNode(N);
	}
}

void ImportQuestGraphLevel(UEdGraph* TargetGraph, const FString& GraphCell, const FQuestDataBundle& Bundle,
					  const TMap<FString, const FQuestDataRow*>& NodeRowsByKey,
					  TMap<FString, UEdGraphNode*>& NodeByKey, TSet<FString>& Consumed, TArray<FString>& Warnings)
{
	ClearDefaultNodes(TargetGraph);   // populate entirely from rows (incl. the exported Entry) - no double-Entry

	for (const auto& Pair : NodeRowsByKey)
	{
		const FQuestDataRow* Row = Pair.Value;
		if (Row->Get(TEXT("graph")) != GraphCell) continue;

		UEdGraphNode* Node = SpawnQuestNodeFromRow(TargetGraph, *Row, Bundle, NodeByKey, Consumed, Warnings);
		if (!Node) continue;

		if (UQuestlineNode_Quest* Quest = Cast<UQuestlineNode_Quest>(Node))
		{
			UEdGraph* Inner = Quest->GetInnerGraph();   // exists post-Finalize (PostPlacedNewNode -> CreateInnerGraph)
			if (!Inner) { Warnings.Add(FString::Printf(TEXT("container '%s' has no inner graph"), *Row->Key)); continue; }
			ImportQuestGraphLevel(Inner, Row->Key, Bundle, NodeRowsByKey, NodeByKey, Consumed, Warnings);
		}
	}
}

/**
 * After all nodes exist + properties are restored, regenerate property-derived pins by calling each node type's
 * refresh hook. ORDER MATTERS: a container's outcome pins derive from its inner graph's Exits, so inner graphs must
 * be refreshed before their containers. We achieve innermost-first by processing nodes in descending graph DEPTH
 * (depth = how many container-key hops from root). LinkedQuestline derives from the on-disk linked asset (order-
 * independent). Step/Entry derive from their own restored properties (order-independent).
 */
static int32 GraphDepthOf(const FQuestDataRow* Row, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey)
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

void RefreshQuestNodePins(const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey,
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

UEdGraphPin* ResolveQuestSourcePin(UEdGraphNode* Node, const FString& EdgeType)
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

/**
 * The DESTINATION input category isn't always the source output category. Outcome outputs (QuestOutcome) route
 * into a target's activation-style input (QuestActivation) - an Exit's "Outcome" pin, or a content node's
 * "Activate". Prereq and activation and deactivation wires keep their category across the wire. Map source
 * category -> the category the destination exposes for that wire kind.
 */
static FName ResolveDestCategory(FName SourceCategory)
{
	// Outcome outputs route into activation-style inputs; deactivation OUTPUTS (past-tense "QuestDeactivated")
	// route into deactivation INPUTS (present-tense "QuestDeactivate"). Activation + prerequisite match across.
	if (SourceCategory == TEXT("QuestOutcome"))      return TEXT("QuestActivation");
	if (SourceCategory == TEXT("QuestDeactivated"))  return TEXT("QuestDeactivate");
	return SourceCategory;
}

UEdGraphPin* ResolveQuestDestPin(UEdGraphNode* Node, FName SourceCategory)
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

void WireQuestEdges(const FQuestDataBundle& Bundle, const TMap<FString, UEdGraphNode*>& NodeByKey,
               const TSet<FString>& ConsumedChildKeys, TArray<FString>& Warnings)
{
	for (const FQuestDataEdge& E : Bundle.Edges)
	{
		// contains edges are NOT wiring - they're the instanced-reattach record. Cross-check (D1's completeness
		// property, kept as a tripwire): every contains edge's child must have been consumed by the property walk.
		if (E.Type.StartsWith(TEXT("contains")))
		{
			// Two distinct "contains" kinds share the verb: contains(InnerGraph) is a container->inner-NODE edge
			// (those nodes are spawned by ImportQuestGraphLevel, not reattached), while contains(<prop>[i]) is an
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
		UEdGraphPin* SourcePin = ResolveQuestSourcePin(*FromNode, E.Type);
		if (!SourcePin) { Warnings.Add(FString::Printf(TEXT("no source pin for edge %s %s"), *E.From, *E.Type)); continue; }
		UEdGraphPin* DestPin = ResolveQuestDestPin(*ToNode, SourcePin->PinType.PinCategory);
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

