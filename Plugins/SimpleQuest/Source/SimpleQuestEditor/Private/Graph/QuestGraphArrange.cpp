// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Graph/QuestGraphArrange.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Types/QuestPinRole.h"


namespace
{
	/** The stable within-rank key. Empty for anything that is not a questline node, which is how furniture is skipped. */
	FString OrderKeyFor(const UEdGraphNode* Node)
	{
		const UQuestlineNodeBase* Quest = Cast<UQuestlineNodeBase>(Node);
		return Quest ? Quest->QuestGuid.ToString(EGuidFormats::Digits) : FString();
	}

	bool IsKnot(const UEdGraphNode* Node)
	{
		const UQuestlineNodeBase* Quest = Cast<UQuestlineNodeBase>(Node);
		return Quest && Quest->IsPassThroughNode();
	}

	/**
	 * Every destination pin reachable from FromPin with knots collapsed away. A knot carries no meaning, so an edge
	 * THROUGH one is an edge between the nodes at its ends - ranking a knot as a node would scatter a self-loop arch
	 * across columns and tear it apart.
	 */
	void CollectCollapsedTargets(const UEdGraphPin* FromPin, TSet<const UEdGraphNode*>& VisitedKnots,
								 TArray<UEdGraphPin*>& OutDestPins)
	{
		if (!FromPin) { return; }

		for (UEdGraphPin* Linked : FromPin->LinkedTo)
		{
			if (!Linked) { continue; }
			UEdGraphNode* Owner = Linked->GetOwningNode();
			if (!Owner) { continue; }

			if (!IsKnot(Owner))
			{
				OutDestPins.Add(Linked);
				continue;
			}

			// A knot chain can legally loop - a self-loop arch is two knots - so the visited set is what terminates
			// this rather than an assumption that chains are short.
			if (VisitedKnots.Contains(Owner)) { continue; }
			VisitedKnots.Add(Owner);
			for (UEdGraphPin* Out : Owner->Pins)
			{
				if (Out && Out->Direction == EGPD_Output) { CollectCollapsedTargets(Out, VisitedKnots, OutDestPins); }
			}
		}
	}

	/** Does an edge landing on this pin PROGRESS the graph - should it push the receiving node rightward? */
	bool IsRankingDestination(const UEdGraphPin* Pin)
	{
		// DIRECTION + CATEGORY, deliberately not GetPinRole. Two pins in this codebase fall through the role map to
		// None - Exit's input is named "Outcome" rather than "Activate", and PrerequisiteAnd's output is named "Out" -
		// so a role-based test drops every wire into an Exit and ranks a fully connected terminal at 0. Category is
		// also the more durable test: a node type added later with a differently-named pin still classifies correctly.
		if (!Pin || Pin->Direction != EGPD_Input) { return false; }
		const FName Cat = Pin->PinType.PinCategory;
		return Cat == TEXT("QuestActivation") || Cat == TEXT("QuestPrerequisite");
	}

	bool IsDeactivationDestination(const UEdGraphPin* Pin)
	{
		return Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == TEXT("QuestDeactivate");
	}

	/**
	 * Can anything feed this node at all? Structural, not topological - it asks about the node's PINS, not its wires.
	 * Rank 0 means "nothing can come before this", which is only true of a node with no inputs to receive through:
	 * Entry, and the portal Exits that emit into a graph. A node that CAN be fed starts at 1 even when nothing feeds
	 * it today, because an unwired Exit sitting in the leftmost column reads as a starting point, which it is not.
	 */
	bool HasAnyInputPin(const UEdGraphNode* Node)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input) { return true; }
		}
		return false;
	}

	bool HasAnyOutputPin(const UEdGraphNode* Node)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output) { return true; }
		}
		return false;
	}

	bool IsExitNode(const UEdGraphNode* Node)
	{
		const UQuestlineNodeBase* Quest = Cast<UQuestlineNodeBase>(Node);
		return Quest && Quest->IsExitNode();
	}
}

void RankQuestGraphNodes(const UEdGraph& Graph, TArray<FQuestNodeRank>& OutRanks)
{
	OutRanks.Reset();

	// Rankable = questline nodes that are not knots. Comment boxes and other furniture carry no OrderKey and are left
	// out entirely rather than ranked into a column of their own.
	TArray<UEdGraphNode*> Nodes;
	TMap<UEdGraphNode*, FString> KeyByNode;
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		if (!Node || IsKnot(Node)) { continue; }
		const FString Key = OrderKeyFor(Node);
		if (Key.IsEmpty()) { continue; }
		Nodes.Add(Node);
		KeyByNode.Add(Node, Key);
	}
	// EVERY later traversal starts from this order, which is what makes the whole pass reproducible. Sorting on the
	// key rather than on Graph.Nodes matters: that array is TMap hash order on a fresh import.
	Nodes.Sort([&KeyByNode](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return KeyByNode[const_cast<UEdGraphNode*>(&A)] < KeyByNode[const_cast<UEdGraphNode*>(&B)];
	});

	// Two relations, built in one walk. Primary is what ranks; Secondary exists only to rescue a node whose ONLY
	// structure is a teardown wire.
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Primary;
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Secondary;
	for (UEdGraphNode* Node : Nodes)
	{
		Primary.Add(Node);
		Secondary.Add(Node);
	}

	for (UEdGraphNode* Node : Nodes)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) { continue; }

			TSet<const UEdGraphNode*> VisitedKnots;
			TArray<UEdGraphPin*> Dests;
			CollectCollapsedTargets(Pin, VisitedKnots, Dests);

			for (const UEdGraphPin* Dest : Dests)
			{
				UEdGraphNode* DestNode = Dest->GetOwningNode();
				if (!DestNode || DestNode == Node || !Primary.Contains(DestNode)) { continue; }   // self-loops rank nothing

				if (IsRankingDestination(Dest))           { Primary[Node].AddUnique(DestNode); }
				else if (IsDeactivationDestination(Dest)) { Secondary[Node].AddUnique(DestNode); }
			}
		}
	}

	// A node with NO exec or prereq wiring at all is invisible to the primary relation, and if its only wires are
	// deactivation ones it would sit alone at rank 0 saying nothing. Fold those in - and ONLY those, so a teardown
	// wire never drags a node that already has real structure.
	TSet<UEdGraphNode*> HasPrimary;
	for (const TPair<UEdGraphNode*, TArray<UEdGraphNode*>>& Pair : Primary)
	{
		if (Pair.Value.Num() > 0) { HasPrimary.Add(Pair.Key); for (UEdGraphNode* D : Pair.Value) { HasPrimary.Add(D); } }
	}
	for (UEdGraphNode* Node : Nodes)
	{
		if (HasPrimary.Contains(Node)) { continue; }
		for (UEdGraphNode* Dest : Secondary[Node])
		{
			if (!HasPrimary.Contains(Dest)) { Primary[Node].AddUnique(Dest); }
		}
	}

	// IN-DEGREE, for seeding. A source is a node nothing points at - Entry is one of these, not the definition of one.
	TMap<UEdGraphNode*, int32> InDegree;
	for (UEdGraphNode* Node : Nodes) { InDegree.Add(Node, 0); }
	for (UEdGraphNode* Node : Nodes)
	{
		for (UEdGraphNode* Dest : Primary[Node]) { ++InDegree[Dest]; }
	}

	// CYCLE BREAK. DFS from the sources first, in key order, then from anything still unvisited. Seeding from sources
	// is load-bearing: a naive DFS that wanders into a branch before reaching a hub classifies hub->branch as the back
	// edge instead of branch->hub, and the entire graph then reads backwards.
	TSet<TPair<UEdGraphNode*, UEdGraphNode*>> BackEdges;
	TSet<UEdGraphNode*> Visited;
	TSet<UEdGraphNode*> OnStack;

	TFunction<void(UEdGraphNode*)> Walk = [&](UEdGraphNode* Node)
	{
		Visited.Add(Node);
		OnStack.Add(Node);
		for (UEdGraphNode* Dest : Primary[Node])
		{
			if (OnStack.Contains(Dest))      { BackEdges.Add({ Node, Dest }); }
			else if (!Visited.Contains(Dest)) { Walk(Dest); }
		}
		OnStack.Remove(Node);
	};

	for (UEdGraphNode* Node : Nodes) { if (InDegree[Node] == 0 && !Visited.Contains(Node)) { Walk(Node); } }
	// Whatever remains is a pure cycle with no way in. It breaks at an arbitrary point - but a STABLE one, since Nodes
	// is key-ordered, which is what the determinism requirement actually asks for.
	for (UEdGraphNode* Node : Nodes) { if (!Visited.Contains(Node)) { Walk(Node); } }

	// LONGEST PATH over what is left, which is now a DAG. Kahn's, with the frontier kept in key order so the traversal
	// is reproducible even where ranks tie.
	TMap<UEdGraphNode*, int32> Remaining;
	TMap<UEdGraphNode*, int32> Rank;
	
	// The floor doubles as the seed: relaxation only ever raises a rank, so initialising to 1 for anything with an
	// input pin reserves column 0 for the nodes that genuinely cannot be preceded.
	for (UEdGraphNode* Node : Nodes) { Remaining.Add(Node, 0); Rank.Add(Node, HasAnyInputPin(Node) ? 1 : 0); }
	
	for (UEdGraphNode* Node : Nodes)
	{
		for (UEdGraphNode* Dest : Primary[Node])
		{
			if (!BackEdges.Contains({ Node, Dest })) { ++Remaining[Dest]; }
		}
	}

	TArray<UEdGraphNode*> Frontier;
	for (UEdGraphNode* Node : Nodes) { if (Remaining[Node] == 0) { Frontier.Add(Node); } }

	while (Frontier.Num() > 0)
	{
		UEdGraphNode* Node = Frontier[0];
		Frontier.RemoveAt(0);
		for (UEdGraphNode* Dest : Primary[Node])
		{
			if (BackEdges.Contains({ Node, Dest })) { continue; }
			Rank[Dest] = FMath::Max(Rank[Dest], Rank[Node] + 1);
			if (--Remaining[Dest] == 0)
			{
				// Inserted in key order rather than appended, so the frontier never depends on discovery order.
				const FString& DestKey = KeyByNode[Dest];
				int32 At = 0;
				while (At < Frontier.Num() && KeyByNode[Frontier[At]] < DestKey) { ++At; }
				Frontier.Insert(Dest, At);
			}
		}
	}

	// AN UNCONNECTED EXIT JOINS THE OTHER TERMINALS. Measured against the deepest node that still has OUTPUTS, not the
	// deepest node overall - an Exit has none, so excluding terminals from the max lands a loose Exit in the SAME
	// column as the connected ones rather than one past them. Terminals read as a column; a lone Exit a step further
	// right reads as a stage nothing reaches.
	int32 MaxProducerRank = 0;
	for (UEdGraphNode* Node : Nodes)
	{
		if (HasAnyOutputPin(Node)) { MaxProducerRank = FMath::Max(MaxProducerRank, Rank[Node]); }
	}
	for (UEdGraphNode* Node : Nodes)
	{
		if (InDegree[Node] == 0 && IsExitNode(Node)) { Rank[Node] = MaxProducerRank + 1; }
	}

	OutRanks.Reserve(Nodes.Num());
	for (UEdGraphNode* Node : Nodes)
	{
		OutRanks.Add({ Node, Rank[Node], KeyByNode[Node] });
	}
	OutRanks.Sort([](const FQuestNodeRank& A, const FQuestNodeRank& B)
	{
		return A.Rank != B.Rank ? A.Rank < B.Rank : A.OrderKey < B.OrderKey;
	});
}

