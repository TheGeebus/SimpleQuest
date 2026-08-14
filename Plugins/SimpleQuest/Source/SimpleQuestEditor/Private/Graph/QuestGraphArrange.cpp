// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Graph/QuestGraphArrange.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Quest.h"


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

	/**
	 * Which row a pin occupies in its own direction's stack, top to bottom - the order SGraphNode draws them in. Inputs
	 * and outputs stack independently, so a node's third output is row 2 regardless of how many inputs sit beside it.
	 */
	int32 PinRow(const UEdGraphPin* Pin)
	{
		const UEdGraphNode* Owner = Pin ? Pin->GetOwningNode() : nullptr;
		if (!Owner) { return 0; }

		int32 Row = 0;
		for (const UEdGraphPin* Other : Owner->Pins)
		{
			if (Other == Pin) { break; }
			if (Other && Other->Direction == Pin->Direction && !Other->bHidden) { ++Row; }
		}
		return Row;
	}

	/**
	 * One collapsed edge, carrying WHERE ON EACH NODE it attaches. Ranking ignores the rows; placement cannot, because
	 * two siblings fed by one node are indistinguishable without them and the ordering falls to the GUID tiebreak.
	 */
	struct FQuestGraphEdge
	{
		UEdGraphNode* Target = nullptr;
		int32 SourceRow = 0;   // row among the SOURCE node's output pins
		int32 TargetRow = 0;   // row among the TARGET node's input pins
	};

	/**
	 * At most one edge per node pair, keeping the TOPMOST source row. Parallel wires between the same two nodes are rare
	 * and the eye follows the upper one; more to the point, ranking counts edges to know when a node's predecessors are
	 * all resolved, and a back edge is recorded per node pair - so admitting parallel edges would leave a node's counter
	 * permanently above zero and strand it at its floor rank.
	 */
	void AddOrTightenEdge(TArray<FQuestGraphEdge>& Edges, const FQuestGraphEdge& New)
	{
		for (FQuestGraphEdge& Existing : Edges)
		{
			if (Existing.Target != New.Target) { continue; }
			if (New.SourceRow < Existing.SourceRow) { Existing = New; }
			return;
		}
		Edges.Add(New);
	}

	/** The knot-collapsed graph, built once and used by both ranking and placement. */
	struct FQuestGraphRelation
	{
		TArray<UEdGraphNode*> Nodes;                                  // key-ordered; the seed of every traversal
		TMap<UEdGraphNode*, FString> KeyByNode;
		TMap<UEdGraphNode*, TArray<FQuestGraphEdge>> Primary;         // ranks
		TMap<UEdGraphNode*, TArray<FQuestGraphEdge>> Secondary;       // deactivation; never ranks, only clarifies
		TMap<UEdGraphNode*, int32> InDegree;
	};

	void BuildQuestGraphRelation(const UEdGraph& Graph, FQuestGraphRelation& Out)
	{
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (!Node || IsKnot(Node)) { continue; }
			const FString Key = OrderKeyFor(Node);
			if (Key.IsEmpty()) { continue; }
			Out.Nodes.Add(Node);
			Out.KeyByNode.Add(Node, Key);
		}
		Out.Nodes.Sort([&Out](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return Out.KeyByNode[const_cast<UEdGraphNode*>(&A)] < Out.KeyByNode[const_cast<UEdGraphNode*>(&B)];
		});

		for (UEdGraphNode* Node : Out.Nodes) { Out.Primary.Add(Node); Out.Secondary.Add(Node); }

		for (UEdGraphNode* Node : Out.Nodes)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) { continue; }
				TSet<const UEdGraphNode*> VisitedKnots;
				TArray<UEdGraphPin*> Dests;
				CollectCollapsedTargets(Pin, VisitedKnots, Dests);

				// The source row comes from THIS pin, not from wherever the knot chain surfaced - a knot is drawn on the
				// wire, so the wire still leaves the pin the designer plugged it into.
				const int32 SourceRow = PinRow(Pin);

				for (const UEdGraphPin* Dest : Dests)
				{
					UEdGraphNode* DestNode = Dest->GetOwningNode();
					if (!DestNode || DestNode == Node || !Out.Primary.Contains(DestNode)) { continue; }
					const FQuestGraphEdge Edge{ DestNode, SourceRow, PinRow(Dest) };
					if (IsRankingDestination(Dest))           { AddOrTightenEdge(Out.Primary[Node], Edge); }
					else if (IsDeactivationDestination(Dest)) { AddOrTightenEdge(Out.Secondary[Node], Edge); }
				}
			}
		}

		TSet<UEdGraphNode*> HasPrimary;
		for (const TPair<UEdGraphNode*, TArray<FQuestGraphEdge>>& Pair : Out.Primary)
		{
			if (Pair.Value.Num() > 0)
			{
				HasPrimary.Add(Pair.Key);
				for (const FQuestGraphEdge& E : Pair.Value) { HasPrimary.Add(E.Target); }
			}
		}
		for (UEdGraphNode* Node : Out.Nodes)
		{
			if (HasPrimary.Contains(Node)) { continue; }
			for (const FQuestGraphEdge& E : Out.Secondary[Node])
			{
				if (!HasPrimary.Contains(E.Target)) { AddOrTightenEdge(Out.Primary[Node], E); }
			}
		}

		for (UEdGraphNode* Node : Out.Nodes) { Out.InDegree.Add(Node, 0); }
		for (UEdGraphNode* Node : Out.Nodes)
		{
			for (const FQuestGraphEdge& E : Out.Primary[Node]) { ++Out.InDegree[E.Target]; }
		}
	}
}

void RankQuestGraphNodes(const UEdGraph& Graph, TArray<FQuestNodeRank>& OutRanks)
{
	OutRanks.Reset();

	FQuestGraphRelation Rel;
	BuildQuestGraphRelation(Graph, Rel);
	TArray<UEdGraphNode*>& Nodes = Rel.Nodes;
	TMap<UEdGraphNode*, FString>& KeyByNode = Rel.KeyByNode;
	TMap<UEdGraphNode*, TArray<FQuestGraphEdge>>& Primary = Rel.Primary;
	TMap<UEdGraphNode*, int32>& InDegree = Rel.InDegree;

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
		for (const FQuestGraphEdge& E : Primary[Node])
		{
			if (OnStack.Contains(E.Target))       { BackEdges.Add({ Node, E.Target }); }
			else if (!Visited.Contains(E.Target)) { Walk(E.Target); }
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
		for (const FQuestGraphEdge& E : Primary[Node])
		{
			if (!BackEdges.Contains({ Node, E.Target })) { ++Remaining[E.Target]; }
		}
	}

	TArray<UEdGraphNode*> Frontier;
	for (UEdGraphNode* Node : Nodes) { if (Remaining[Node] == 0) { Frontier.Add(Node); } }

	while (Frontier.Num() > 0)
	{
		UEdGraphNode* Node = Frontier[0];
		Frontier.RemoveAt(0);
		for (const FQuestGraphEdge& E : Primary[Node])
		{
			UEdGraphNode* Dest = E.Target;
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

namespace
{
	// Column pitch is a CONSTANT because node WIDTH is unknowable without a widget - sized for a content node with a
	// long label. Height is estimated rather than guessed, because pin count IS knowable, and a Step's outcome pins are
	// data-derived: a two-outcome Step and a six-outcome one are very different objects.
	constexpr float GColumnPitch    = 420.0f;
	constexpr float GRowGap         = 48.0f;
	constexpr float GNodeBaseHeight = 64.0f;
	constexpr float GPinHeight      = 22.0f;

	float EstimatedHeight(const UEdGraphNode* Node)
	{
		int32 In = 0, Out = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) { continue; }
			(Pin->Direction == EGPD_Input ? In : Out)++;
		}
		return GNodeBaseHeight + FMath::Max(In, Out) * GPinHeight;
	}

	UEdGraphNode* FindEntryNode(const UEdGraph& Graph)
	{
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (Cast<UQuestlineNode_Entry>(Node)) { return Node; }
		}
		return nullptr;
	}
}

int32 ArrangeQuestGraph(UEdGraph& Graph, bool bRecurseIntoContainers)
{
	TArray<FQuestNodeRank> Ranks;
	RankQuestGraphNodes(Graph, Ranks);
	if (Ranks.Num() == 0) { return 0; }

	FQuestGraphRelation Rel;
	BuildQuestGraphRelation(Graph, Rel);

	// INCOMING EDGES for the barycentre, over BOTH relations. Deactivation edges cannot move a node to another column,
	// but they can pull it next to whatever it tears down - the "context clarifier" role they were given.
	struct FIncoming { UEdGraphNode* Source; int32 SourceRow; int32 TargetRow; };
	TMap<UEdGraphNode*, TArray<FIncoming>> Incoming;
	for (UEdGraphNode* Node : Rel.Nodes) { Incoming.Add(Node); }
	for (UEdGraphNode* Node : Rel.Nodes)
	{
		for (const FQuestGraphEdge& E : Rel.Primary[Node])   { Incoming[E.Target].Add({ Node, E.SourceRow, E.TargetRow }); }
		for (const FQuestGraphEdge& E : Rel.Secondary[Node]) { Incoming[E.Target].Add({ Node, E.SourceRow, E.TargetRow }); }
	}

	// ANCHOR. Entry's current position IS the origin: unmoved it is (0,0), and moved it is wherever the designer put
	// it - one rule, both cases, nothing to detect.
	UEdGraphNode* Entry = FindEntryNode(Graph);
	const int32 AnchorX = Entry ? Entry->NodePosX : 0;
	const int32 AnchorY = Entry ? Entry->NodePosY : 0;

	TMap<UEdGraphNode*, float> AssignedY;
	int32 Moved = 0;
	int32 Index = 0;

	while (Index < Ranks.Num())
	{
		const int32 ThisRank = Ranks[Index].Rank;
		TArray<UEdGraphNode*> Column;
		while (Index < Ranks.Num() && Ranks[Index].Rank == ThisRank) { Column.Add(Ranks[Index++].Node); }

		// WHERE THE WIRES WANT THIS NODE. Ranks arrive left-to-right, so every predecessor in an earlier column already
		// has a Y. Averaging the SOURCE PINS rather than the source nodes is what makes a fan-out come out in pin order:
		// siblings hanging off one node share its Y exactly and would otherwise tie, handing the decision to the GUID.
		// Subtracting the target row aims the node so its own receiving pin lines up with the wire, which is the part
		// that makes a long chain read level. Both nodes are assumed to have the same header depth - untrue for a Step
		// carrying a picker widget, and one of the things real geometry would fix.
		TMap<UEdGraphNode*, float> Desired;
		for (UEdGraphNode* Node : Column)
		{
			float Sum = 0.0f; int32 Count = 0;
			for (const FIncoming& In : Incoming[Node])
			{
				if (const float* Y = AssignedY.Find(In.Source))
				{
					Sum += *Y + (In.SourceRow - In.TargetRow) * GPinHeight;
					++Count;
				}
			}
			// No placed predecessor sorts last on a sentinel rather than falling in beside connected nodes - it has
			// nothing to be near, and pretending otherwise scatters the ones that do.
			Desired.Add(Node, Count > 0 ? Sum / Count : TNumericLimits<float>::Max());
		}

		Column.Sort([&Desired, &Rel, Entry](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			// Entry first in its column, so it keeps the anchor exactly rather than being displaced by whatever else
			// happens to share rank 0 - the portal Exits, which have no inputs either.
			if (&A == Entry) { return true; }
			if (&B == Entry) { return false; }
			UEdGraphNode* NA = const_cast<UEdGraphNode*>(&A);
			UEdGraphNode* NB = const_cast<UEdGraphNode*>(&B);
			return Desired[NA] != Desired[NB] ? Desired[NA] < Desired[NB] : Rel.KeyByNode[NA] < Rel.KeyByNode[NB];
		});

		float Cursor = 0.0f;
		for (UEdGraphNode* Node : Column)
		{
			// Honour the desired Y only DOWNWARD. Pulling a node up to straighten its wire would overlap the node above
			// it, and no wire is worth a collision; pushing down costs empty space, which reads as breathing room.
			const float* Want = Desired.Find(Node);
			const float Y = (Want && *Want < TNumericLimits<float>::Max()) ? FMath::Max(Cursor, *Want) : Cursor;

			const int32 NewX = AnchorX + ThisRank * FMath::RoundToInt(GColumnPitch);
			const int32 NewY = AnchorY + FMath::RoundToInt(Y);
			AssignedY.Add(Node, Y);

			if (Node->NodePosX != NewX || Node->NodePosY != NewY)
			{
				Node->Modify();
				Node->NodePosX = NewX;
				Node->NodePosY = NewY;
				++Moved;
			}
			Cursor = Y + EstimatedHeight(Node) + GRowGap;
		}
	}

	if (bRecurseIntoContainers)
	{
		// Each inner graph is its own layered reading with its own Entry at its own origin - a container is a plain box
		// to the outer pass, which is why the outer layout needs to know nothing about what is inside it.
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (const UQuestlineNode_Quest* Quest = Cast<UQuestlineNode_Quest>(Node))
			{
				if (UEdGraph* Inner = Quest->GetInnerGraph()) { Moved += ArrangeQuestGraph(*Inner, true); }
			}
		}
	}

	if (Moved > 0) { Graph.NotifyGraphChanged(); }
	return Moved;
}

