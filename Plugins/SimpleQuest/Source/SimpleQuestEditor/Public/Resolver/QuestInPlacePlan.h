// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// What a re-import WOULD do to an existing questline asset, computed without touching it. Deliberately plain data: it holds
// keys and display strings, never live node pointers, so it can be produced by the import path, handed to a panel, examined
// by a designer, and applied on a later frame without holding references across a garbage collection.

#include "CoreMinimal.h"
#include "QuestDataBundle.h"
#include "QuestDataValue.h"

/** What happens to one node when the plan is applied. */
enum class EQuestNodePlanAction : uint8
{
	Update,   // the source and the asset both have it
	Create,   // the source has it, the asset does not
	Orphan,   // the asset has it, the source does not
};

/**
 * What a single change IS. Three genuinely different things shared one struct and were told apart by sentinel strings in
 * the display text, which meant anything wanting to render them differently had to match on prose.
 */
enum class EQuestPropertyChangeKind : uint8
{
	Edit,          // a value differs between the asset and the source
	ChildAdded,    // the source declares an instanced child the asset does not have
	ChildRemoved,  // the asset holds an instanced child the source does not mention
};

struct FQuestPropertyChange
{
	FString Property;		// a property NAME, or a path like "Rewards[0].Amount" once nested values are described
	EQuestPropertyChangeKind Kind = EQuestPropertyChangeKind::Edit;
	FString CurrentText;    // what the asset holds now, for display
	FString IncomingText;   // what the source would write, for display

	// The value the apply step must WRITE. Carried as data rather than re-derived from the row, because the row alone is not
	// enough: the restore path deliberately leaves some cells unwritten, so "what the source says" and "what the property
	// would end up holding" are different questions. Planning answers the second one; re-answering it at apply time from the
	// first is how a plan and its application drift. Plain data, so it survives between being shown and being applied.
	FQuestDataValue IncomingValue;
};

/** One node's disposition, plus the properties that would actually change. */
struct FQuestNodePlanEntry
{
	FString Key;                 // the row key: a studio-authored id, or our exported GUID

	/**
	 * WHERE the row lives in the destination table, which is a different fact from what it is CALLED. When a recipe names
	 * a KeyColumn the studio's key is a CELL, and the DataTable's row name is theirs to choose - so Key answers "which
	 * row is this" and this answers "where do I write it". Equal to Key when no KeyColumn is set, which covers every
	 * table keyed by row name. Empty on an inbound plan: a questline has no row names.
	 */
	FName DestinationRowName;

	/**
	 * What the node calls itself in the editor, for display only - never for matching. A canonical export keys every row
	 * by GUID, so without this a plan reads as a wall of hex. Empty when there is nothing to ask: a Create has no live
	 * node yet, and takes whatever the incoming row offers or nothing at all.
	 */
	FString Label;
	FString Guid;                // the existing node's GUID; empty for Create
	FString ClassName;           // incoming class (Orphan entries carry the existing class)
	FString CurrentClassName;    // existing class; empty for Create
	FString GraphCell;           // incoming graph level
	FString CurrentGraphCell;    // existing graph level; empty for Create
	
	/**
	 * The same two levels as a reader would recognise them. A level is named by its CONTAINER'S KEY, which in a canonical
	 * export is a GUID - so a panel rendering the raw cell shows a wall of hex for the one question a designer is asking:
	 * which container. Resolved by the planner because only the planner has the nodes. Falls back to the raw cell when the
	 * level is "root" or names something no longer present.
	 */
	FString GraphLabel;
	FString CurrentGraphLabel;

	EQuestNodePlanAction Action = EQuestNodePlanAction::Update;
	TArray<FQuestPropertyChange> Changes;

	/**
	 * True when the node belongs in a different container than the one it currently sits in. A MOVE, not a rebuild: the
	 * node keeps its identity, its properties, its position and - if it is a container - its inner graph and everything
	 * inside it. A class difference is deliberately NOT recorded here. A node whose class differs is a different node,
	 * so it is refused rather than described; see the refusal in PlanQuestInPlace.
	 */
	bool bMoved = false;

	/** True for the one entry describing the questline asset itself rather than a node in it. */
	bool bIsQuestlineSelf = false;
};

/**
 * Which way a plan points. Carried on the PLAN rather than on the broker's record because FOnPlanPublished broadcasts
 * the plan alone - an observer that had to go re-find the record to learn the direction is one that can render a plan
 * the wrong way round, and the panel subscribes to exactly that delegate.
 */
enum class EQuestPlanDirection : uint8
{
	IntoGraph,   // rows from a source describe changes to a questline asset
	IntoTable,   // a questline describes changes to rows in someone else's Data Table
};

/** The whole comparison. */
struct FQuestInPlacePlan
{
	/**
	 * The QUESTLINE this plan belongs to, both ways round: what an inbound plan would change, and what an outbound plan was
	 * built FROM. It is also the broker's key, because the panel looks a plan up by the asset it has open - so it is not free
	 * to become "whatever changes" when the direction flips.
	 */
	FString TargetAssetPath;

	/**
	 * The Data Table an outbound plan would WRITE INTO. Empty on an inbound plan, which changes the questline itself. It
	 * needs its own field precisely because the one above is spoken for: with a single path on the plan, every rendering of
	 * a row plan names the graph it came from while describing changes to a table it never names - and "rows there were left
	 * alone" is not something a reviewer can act on when "there" is missing.
	 */
	FString DestinationAssetPath;
	
	/**
	 * Fingerprint of the in-memory files this plan was built from. Zero for a plan read from a folder or a Data Table,
	 * where the source is still there to be re-read and compared directly.
	 *
	 * Applying a memory-sourced plan requires handing back the same files. This is what makes that checkable: a caller
	 * who passes a different map gets a refusal naming the mismatch, rather than an apply of data nobody reviewed.
	 */
	uint32 SourceFingerprint = 0;

	TArray<FQuestNodePlanEntry> Entries;
	TArray<FString> Warnings;

	/** Keys that name more than one node. Those rows are not planned and those nodes are not orphaned — the caller refuses. */
	TArray<FString> AmbiguousKeys;

	/** Rows the apply step could not deliver — an unresolvable class, or a level nothing declares. Reported, never planned. */
	TArray<FString> Refusals;

	/** Nodes outside every level the source declares. Counted, never entered: a narrow source must not orphan the whole asset. */
	int32 UntouchedNodeCount = 0;

	EQuestPlanDirection Direction = EQuestPlanDirection::IntoGraph;

	/**
	 * Rows in the destination table this graph holds no claim on. Counted, never entered - a graph describing some rows
	 * says nothing about the rest of someone else's table. Deliberately NOT UntouchedNodeCount: "outside every level the
	 * source declares" and "unclaimed by any node" are different statements, and sharing the name is how the next reader
	 * conflates them.
	 */
	int32 UnclaimedRowCount = 0;

	/**
	 * Canonicalized wiring deltas, as DATA rather than display text — apply needs the endpoints and the pin, and re-parsing
	 * a formatted string to recover them would be a lossy round-trip through our own output.
	 */
	TArray<FQuestDataEdge> AddedEdges;
	TArray<FQuestDataEdge> RemovedEdges;
	
	/**
	 * Every key this plan mentions, as a reader would recognise it. Edges reference nodes by KEY, which in a canonical
	 * export is a GUID - so a panel rendering an edge shows two walls of hex for the one question being asked: which
	 * nodes. Built by the planner because only the planner has the nodes; a key with no live node stays absent, and the
	 * caller falls back to the key itself.
	 */
	TMap<FString, FString> LabelByKey;

	int32 CountOf(EQuestNodePlanAction Action) const
	{
		int32 N = 0;
		for (const FQuestNodePlanEntry& E : Entries) { if (E.Action == Action) ++N; }
		return N;
	}

	/** Matched nodes that would actually be written — the number that matters when deciding whether to apply at all. */
	int32 ChangedNodeCount() const
	{
		int32 N = 0;
		for (const FQuestNodePlanEntry& E : Entries)
		{
			if (E.Action == EQuestNodePlanAction::Update && (E.Changes.Num() > 0 || E.bMoved)) ++N;
		}
		return N;
	}

	/** Nothing to do: no creations, no orphans, and no matched node differs. */
	bool IsNoOp() const
	{
		return CountOf(EQuestNodePlanAction::Create) == 0 && CountOf(EQuestNodePlanAction::Orphan) == 0
			&& ChangedNodeCount() == 0 && AddedEdges.IsEmpty() && RemovedEdges.IsEmpty() && Refusals.IsEmpty();
	}
};

/** What an apply action actually did. Counts rather than prose, so a caller can report it, assert on it, or render it. */
struct FQuestApplyResult
{
	int32 PropertiesWritten = 0;

	/**
	 * ENTITIES, not nodes: an apply writes a questline's nodes in one direction and a Data Table's rows in the other, so
	 * a counter named for either one lies half the time. NodesMoved and EdgesChanged keep their names deliberately -
	 * those are genuinely graph-only, and sitting at zero is an honest statement about a table rather than a lie.
	 */
	int32 EntitiesCreated   = 0;
	int32 EntitiesDeleted   = 0;

	int32 NodesMoved        = 0;
	int32 EdgesChanged      = 0;
	int32 EntriesDeferred   = 0;   // structural work this step does not perform
	TArray<FString> Skipped;
	bool  bRefused = false;        // the plan was not trustworthy enough to act on any part of
	
	/**
	 * How many graphs were recompiled after the apply - the target plus its linked neighborhood. Zero when nothing was
	 * applied, since a no-op apply leaves the compiled model already correct.
	 */
	int32 GraphsCompiled = 0;

	/**
	 * False when the apply action succeeded but a recompile did NOT. The writes are correct and are deliberately not rolled
	 * back; the asset is simply left needing a compilation, and the caller must say so rather than report clean success.
	 */
	bool bCompileSucceeded = true;

	/**
	 * Did the apply action actually WRITE anything? Deliberately distinct from "was anything skipped or deferred" - a run can
	 * decline work and change nothing, and the two answers drive different decisions: whether to dirty the package,
	 * whether to redraw open graphs, and whether "already matches the source" is an honest thing to print.
	 */
	bool ChangedAnything() const
	{
		return PropertiesWritten > 0 || EntitiesCreated > 0 || NodesMoved > 0 || EdgesChanged > 0 || EntitiesDeleted > 0;
	}
};

/** What an apply action is permitted to do beyond writing properties. Destructive actions are opt-in, never inferred. */
struct FQuestApplyOptions
{
	bool bDeleteOrphanedNodes = false;
};

