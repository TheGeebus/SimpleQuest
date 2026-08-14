// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Where every node belongs in a left-to-right layered reading of a questline graph, computed without touching a single
// position. Ranking is separated from placement deliberately: a wrong rank is invisible once it has been expressed as
// pixels, and every decision that can be subtly wrong lives here.

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

/** One node's place in the layered reading. Knots never appear - they are collapsed through, then placed on their wire. */
struct FQuestNodeRank
{
	UEdGraphNode* Node = nullptr;

	/** Column. 0 is a source: a node nothing feeds. Entry is one source among possibly many, not the only one. */
	int32 Rank = 0;

	/**
	 * The within-rank tie-break: UQuestlineNodeBase::QuestGuid as digits. Chosen because it is on the abstract base so
	 * every node has one, it is serialized, and on the import path it is a pure function of the source row key. The
	 * tempting wrong answer is UEdGraphNode::NodeGuid, which is RANDOM per import and sits right beside it.
	 */
	FString OrderKey;
};

/**
 * Rank every questline node in Graph, sorted by (Rank, OrderKey).
 *
 * Ranks over the KNOT-COLLAPSED relation, classifying each edge by its DESTINATION pin's role - ExecIn and PrereqIn
 * both count, because a node gated by unlock_after has no activation wire at all and is reached only through a
 * prerequisite edge. Deactivation edges never determine rank; they contribute only for a node that has no exec or
 * prereq edges whatsoever, so a node whose only structure is a teardown wire still lands somewhere sensible.
 *
 * Seeds from EVERY in-degree-zero node rather than from Entry: on real content most graph levels contain nodes Entry
 * cannot reach - externally started containers, unwired Exits that still define a container's outcome pins - and those
 * are content, not debris. Rank 0 is reserved structurally for nodes with NO INPUT PINS; anything that could be fed
 * starts at 1 even when nothing currently feeds it.
 *
 * An Exit with nothing feeding it is placed past every other rank rather than at the floor - it is a terminal that was
 * never reached, and both "first" and "in the middle" say something untrue about it.
 *
 * Cycles are expected and intentional (self-loops, hubs re-entered from anywhere). Back edges are found by a DFS
 * seeded from the sources and excluded from ranking; they still draw, running leftward, which is how a loop reads.
 */
void RankQuestGraphNodes(const UEdGraph& Graph, TArray<FQuestNodeRank>& OutRanks);

/**
 * Lay Graph out left-to-right by rank, writing NodePosX / NodePosY. Anchored on the Entry node's CURRENT position, so
 * an untouched graph lays out from the origin and one whose Entry a designer moved lays out from wherever they put it.
 *
 * Knots are deliberately left alone - they carry no meaning and belong on their wire, which is a separate pass.
 *
 * THE CALLER OWNS THE TRANSACTION. This calls Modify() per moved node but opens none, so an arrange invoked from an
 * import undoes as one unit with the import, and the deliberate action wraps its own.
 *
 * @return how many nodes actually moved.
 */
int32 ArrangeQuestGraph(UEdGraph& Graph, bool bRecurseIntoContainers);

