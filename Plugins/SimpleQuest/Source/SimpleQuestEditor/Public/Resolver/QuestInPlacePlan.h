// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// What a re-import WOULD do to an existing questline asset, computed without touching it. Deliberately plain data: it holds
// keys and display strings, never live node pointers, so it can be produced by the import path, handed to a panel, examined
// by a designer, and applied on a later frame without holding references across a garbage collection.

#include "CoreMinimal.h"

/** What happens to one node when the plan is applied. */
enum class EQuestNodePlanAction : uint8
{
	Update,   // the source and the asset both have it
	Create,   // the source has it, the asset does not
	Orphan,   // the asset has it, the source does not
};

/** One property whose value would differ after a re-import. */
struct FQuestPropertyChange
{
	FName   Property;
	FString CurrentText;    // what the asset holds now
	FString IncomingText;   // what the source would write
};

/** One node's disposition, plus the properties that would actually change. */
struct FQuestNodePlanEntry
{
	FString Key;                 // the row key: a studio-authored id, or our exported GUID
	FString Guid;                // the existing node's GUID; empty for Create
	FString ClassName;           // incoming class (Orphan entries carry the existing class)
	FString CurrentClassName;    // existing class; empty for Create
	FString GraphCell;           // incoming graph level
	FString CurrentGraphCell;    // existing graph level; empty for Create

	EQuestNodePlanAction Action = EQuestNodePlanAction::Update;
	TArray<FQuestPropertyChange> Changes;

	/** True when the node would have to be rebuilt rather than edited: its class changed, or it moved to another level. */
	bool bStructuralChange = false;
};

/** The whole comparison. */
struct FQuestInPlacePlan
{
	FString TargetAssetPath;
	TArray<FQuestNodePlanEntry> Entries;
	TArray<FString> Warnings;

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
			if (E.Action == EQuestNodePlanAction::Update && (E.Changes.Num() > 0 || E.bStructuralChange)) ++N;
		}
		return N;
	}

	/** Nothing to do: no creations, no orphans, and no matched node differs. */
	bool IsNoOp() const
	{
		return CountOf(EQuestNodePlanAction::Create) == 0 && CountOf(EQuestNodePlanAction::Orphan) == 0 && ChangedNodeCount() == 0;
	}
};