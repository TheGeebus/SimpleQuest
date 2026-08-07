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

	EQuestNodePlanAction Action = EQuestNodePlanAction::Update;
	TArray<FQuestPropertyChange> Changes;

	/**
	 * True when the node belongs in a different container than the one it currently sits in. A MOVE, not a rebuild: the
	 * node keeps its identity, its properties, its position and - if it is a container - its inner graph and everything
	 * inside it. A class difference is deliberately NOT recorded here. A node whose class differs is a different node,
	 * so it is refused rather than described; see the refusal in PlanInPlace.
	 */
	bool bMoved = false;

	/** True for the one entry describing the questline asset itself rather than a node in it. */
	bool bIsQuestlineSelf = false;
};

/** The whole comparison. */
struct FQuestInPlacePlan
{
	FString TargetAssetPath;
	TArray<FQuestNodePlanEntry> Entries;
	TArray<FString> Warnings;

	/** Keys that name more than one node. Those rows are not planned and those nodes are not orphaned — the caller refuses. */
	TArray<FString> AmbiguousKeys;

	/** Rows the apply step could not deliver — an unresolvable class, or a level nothing declares. Reported, never planned. */
	TArray<FString> Refusals;

	/** Nodes outside every level the source declares. Counted, never entered: a narrow source must not orphan the whole asset. */
	int32 UntouchedNodeCount = 0;

	/**
	 * Canonicalized wiring deltas, as DATA rather than display text — apply needs the endpoints and the pin, and re-parsing
	 * a formatted string to recover them would be a lossy round-trip through our own output.
	 */
	TArray<FQuestDataEdge> AddedEdges;
	TArray<FQuestDataEdge> RemovedEdges;

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

/** What an apply actually did. Counts rather than prose, so a caller can report it, assert on it, or render it. */
struct FQuestApplyResult
{
	int32 PropertiesWritten = 0;
	int32 NodesCreated      = 0;
	int32 NodesDeleted      = 0;
	int32 EdgesChanged      = 0;
	int32 EntriesDeferred   = 0;   // structural work this step does not perform
	TArray<FString> Skipped;
	bool  bRefused = false;        // the plan was not trustworthy enough to act on any part of
};

/** What an apply is permitted to do beyond writing properties. Destructive actions are opt-in, never inferred. */
struct FQuestApplyOptions
{
	bool bDeleteOrphanedNodes = false;
};

