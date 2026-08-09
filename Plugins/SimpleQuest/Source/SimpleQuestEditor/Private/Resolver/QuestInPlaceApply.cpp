// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestInPlaceApply.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestGraphBuilder.h"
#include "Resolver/QuestInstancedChildren.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Resolver/QuestRowRestore.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "UObject/UnrealType.h"
#include "Utilities/SimpleQuestEditorUtils.h"


/**
 * Every object a change's path could name, keyed by the path that names it: "" for the entity itself, then each instanced
 * descendant under the path the WRITER gives it. Built by the same walk the plan used, so any path the plan produced
 * resolves here by construction. The children are reached from a non-const owner, so widening them back is sound.
 */
static void CollectApplyTargets(UObject* Owner, const FString& OwnerKey, const FString& PathPrefix, TMap<FString, UObject*>& OutByPath)
{
	OutByPath.Add(PathPrefix, Owner);
	for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)) continue;
		if (!IsQuestInstancedBearing(Prop)) continue;

		ForEachQuestInstancedChild(Prop, Prop->ContainerPtrToValuePtr<void>(Owner), OwnerKey, Prop->GetName(),
			[&OutByPath](const FString& ChildKey, const FString& Path, const UObject* Child)
			{
				CollectApplyTargets(const_cast<UObject*>(Child), ChildKey, Path, OutByPath);
			});
	}
}

/**
 * Which object owns this change, and under what property name? LONGEST matching path wins, and the candidate must
 * actually have a property by the remaining name - that pair of conditions is what makes this safe without parsing a
 * path whose segments can contain dots and brackets of their own.
 */
static UObject* FindApplyTarget(const TMap<FString, UObject*>& ByPath, const FString& ChangePath, FString& OutPropertyName)
{
	UObject* Best = nullptr;
	int32 BestLen = -1;
	for (const TPair<FString, UObject*>& Pair : ByPath)
	{
		FString Remainder;
		if (Pair.Key.IsEmpty())                                    { Remainder = ChangePath; }
		else if (ChangePath.StartsWith(Pair.Key + TEXT("."))) { Remainder = ChangePath.RightChop(Pair.Key.Len() + 1); }
		else                                                       { continue; }

		if (Remainder.IsEmpty() || Pair.Key.Len() <= BestLen) continue;
		if (Pair.Value->GetClass()->FindPropertyByName(FName(*Remainder)))
		{
			Best = Pair.Value;
			BestLen = Pair.Key.Len();
			OutPropertyName = Remainder;
		}
	}
	return Best;
}

/**
 * Properties that carry IDENTITY rather than configuration. QuestlineID is not a field - it is the questline's compiled
 * tag namespace, so rewriting it moves every tag the questline owns, invalidates save data keyed on them, and can
 * collide with another asset. A rename is a deliberate operation with consequences a property write cannot express, so
 * apply reports it and declines. The plan still SHOWS the difference; it simply is not something this step performs.
 */
static bool IsIdentityBearingProperty(const UObject* Target, const FString& PropertyName)
{
	return Target && Target->IsA<UQuestlineGraph>() && PropertyName == TEXT("QuestlineID");
}

int32 ApplyQuestChangesToObject(UObject* Owner, const FString& OwnerKey, const TArray<FQuestPropertyChange>& Changes, TArray<FString>& OutSkipped)
{
	if (!Owner) return 0;

	TMap<FString, UObject*> ByPath;
	CollectApplyTargets(Owner, OwnerKey, FString(), ByPath);

	int32 Written = 0;
	for (const FQuestPropertyChange& Change : Changes)
	{
		FString PropertyName;
		UObject* Target = FindApplyTarget(ByPath, Change.Property, PropertyName);
		if (!Target)
		{
			// The structure moved between planning and applying, or the plan came from elsewhere. Either way, say so -
			// a change that silently evaporates is worse than one that fails, because the plan already promised it.
			OutSkipped.Add(FString::Printf(TEXT("'%s' names no property reachable from '%s'"), *Change.Property, *OwnerKey));
			continue;
		}
		FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Prop)
		{
			OutSkipped.Add(FString::Printf(TEXT("'%s' resolved to no property"), *Change.Property));
			continue;
		}

		if (IsIdentityBearingProperty(Target, PropertyName))
		{
			OutSkipped.Add(FString::Printf(TEXT("'%s' is identity-bearing - rewriting it would move the questline's compiled "
				"tag namespace and orphan save data keyed on it. Rename deliberately instead."), *Change.Property));
			continue;
		}

		Target->Modify();   // per-object, so undo restores instanced children as well as the node
		// Write the value the PLAN computed, not a re-reading of the row. Once the restore path can decline to write,
		// those are different questions, and answering the second one here is how an apply drifts from its own preview.
		if (!RestoreQuestCell(Prop, Prop->ContainerPtrToValuePtr<void>(Target), Change.IncomingValue))
		{
			// Counting this would report a change that did not happen, and the plan already promised it would. Named
			// rather than merely uncounted - a value the source states and the asset refuses is a source the author
			// needs to fix, not a silent no-op.
			OutSkipped.Add(FString::Printf(TEXT("'%s' could not be written to '%s' - the value does not fit that property"),
				*Change.Property, *OwnerKey));
			continue;
		}
		++Written;
	}
	return Written;
}

/**
 * The graph a level names: "root" is the questline's own graph, anything else names a container node whose inner graph
 * it is. Null means the level names nothing reachable - the planner refuses genuinely unreachable levels, so a null here
 * means either the asset moved under us or the container is itself a create that has not spawned yet.
 */
static UEdGraph* ResolveLevelGraph(UQuestlineGraph& Target, const FString& Level, const TMap<FString, UEdGraphNode*>& NodeByKey)
{
	if (Level.IsEmpty() || Level == TEXT("root")) { return Target.QuestlineEdGraph; }
	if (UEdGraphNode* const* Found = NodeByKey.Find(Level))
	{
		if (UQuestlineNode_Quest* Quest = Cast<UQuestlineNode_Quest>(*Found)) { return Quest->GetInnerGraph(); }
	}
	return nullptr;
}

void ApplyQuestPlan(UQuestlineGraph& Target, const FQuestInPlacePlan& Plan, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, FQuestApplyResult& OutResult, const FQuestApplyOptions& Options)
{
	// A plan carrying refusals or contested keys is untrustworthy in ANY part - those say the planner could not describe
	// the source, not merely that one row is odd. Acting on the rest would be acting on a description known to be wrong.
	if (!Plan.Refusals.IsEmpty() || !Plan.AmbiguousKeys.IsEmpty())
	{
		OutResult.bRefused = true;
		return;
	}

	// Record the questline for undo WITHOUT dirtying: at this point we do not yet know whether anything will change, and
	// UObject::Modify marks the package dirty by default. The caller dirties once, at the end, only if something did.
	// Every other Modify in this function sits inside a branch that is about to write, so those dirty correctly.
	Target.Modify(false);

	// Existing nodes, addressed the way rows address them AND by GUID, since a source may use either spelling.
	TMap<FString, FString> SourceKeyByGuid;
	TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
	CollectQuestNodeIdentity(Target.QuestlineEdGraph, SourceKeyByGuid, NodeByGuid);
	TMap<FString, UEdGraphNode*> NodeByKey;
	for (const TPair<FString, const UQuestlineNodeBase*>& Pair : NodeByGuid)
	{
		UEdGraphNode* Node = const_cast<UQuestlineNodeBase*>(Pair.Value);
		NodeByKey.Add(QuestNodeIdentityKey(Pair.Key, SourceKeyByGuid), Node);
		NodeByKey.Add(Pair.Key, Node);
	}

	// Shared by the create and move passes, consumed by the single pin-refresh below. Declared out here because a move
	// can land in a container a create just made, and both have to reach the same refresh.
	TMap<FString, UEdGraphNode*> PinRefreshTargets;
	TArray<FString> SpawnWarnings;

	// CREATE before anything else: a container has to exist before nodes inside it, and wiring will reference new nodes.
	{
		TArray<const FQuestNodePlanEntry*> Pending;
		for (const FQuestNodePlanEntry& Entry : Plan.Entries)
		{
			if (Entry.Action == EQuestNodePlanAction::Create) { Pending.Add(&Entry); }
		}

		TSet<FString> Consumed;

		// A source may declare a container AND its contents, so one create's level can be another create. Spawn whatever
		// is reachable and repeat while anything moved; the planner already refuses genuinely unreachable levels, so this
		// settles in a pass or two. Anything still pending is REPORTED rather than dropped - a create that quietly does
		// not happen is the failure mode this whole arc exists to prevent.
		bool bProgress = true;
		while (bProgress && Pending.Num() > 0)
		{
			bProgress = false;
			for (int32 Idx = Pending.Num() - 1; Idx >= 0; --Idx)
			{
				const FQuestNodePlanEntry& Entry = *Pending[Idx];
				UEdGraph* Level = ResolveLevelGraph(Target, Entry.GraphCell, NodeByKey);
				if (!Level) { continue; }

				const FQuestDataRow* const* Row = NodeRowsByKey.Find(Entry.Key);
				if (!Row || !*Row)
				{
					OutResult.Skipped.Add(FString::Printf(TEXT("create '%s' has no row in the source"), *Entry.Key));
				}
				else
				{
					Level->Modify();
					if (UEdGraphNode* Node = SpawnQuestNodeFromRow(Level, **Row, Bundle, NodeByKey, Consumed, SpawnWarnings))
					{
						PinRefreshTargets.Add(Entry.Key, Node);
						// The container this landed in advertises outcome pins derived from its inner Exits, so it needs
						// refreshing too - otherwise an edge out of the container cannot resolve a pin that should exist.
						if (UEdGraphNode* Container = NodeByKey.FindRef(Entry.GraphCell)) { PinRefreshTargets.Add(Entry.GraphCell, Container); }
						++OutResult.NodesCreated;
					}
					else
					{
						OutResult.Skipped.Add(FString::Printf(TEXT("create '%s' could not be spawned"), *Entry.Key));
					}
				}
				Pending.RemoveAt(Idx);
				bProgress = true;
			}
		}
		for (const FQuestNodePlanEntry* Entry : Pending)
		{
			OutResult.Skipped.Add(FString::Printf(TEXT("create '%s' sits in level '%s', which never became reachable"),
				*Entry->Key, *Entry->GraphCell));
		}
	}

	// MOVE, after creation so a destination container this same source declares already exists, and before wiring so the
	// delta pass sees final placement. A move is a REPARENT, not a rebuild - the node keeps its identity, properties,
	// position, and, if it is a container, its inner graph and everything in it, because CreateInnerGraph outers that
	// graph to the NODE. Engine precedent: FBlueprintEditor::CollapseNodesIntoGraph.
	// Links are deliberately left alone. A source that relocates a node also restates its wiring, so the plan's edge
	// deltas already describe the post-move topology and the wiring pass below is what applies them.
	{
		for (const FQuestNodePlanEntry& Entry : Plan.Entries)
		{
			if (Entry.Action != EQuestNodePlanAction::Update || !Entry.bMoved) { continue; }

			UEdGraphNode* Node = NodeByKey.FindRef(Entry.Guid);
			UEdGraph* Dest = ResolveLevelGraph(Target, Entry.GraphCell, NodeByKey);
			if (!Node || !Dest)
			{
				OutResult.Skipped.Add(FString::Printf(TEXT("move '%s' could not resolve its %s"), *Entry.Key,
					Node ? TEXT("destination container") : TEXT("node")));
				continue;
			}

			UEdGraph* Source = Node->GetGraph();
			if (Source == Dest) { continue; }   // the plan and the graph already agree; nothing to relocate

			// Modify all three before touching anything: the two node arrays change, and the node's outer changes.
			Source->Modify();
			Dest->Modify();
			Node->Modify();
			Source->Nodes.Remove(Node);
			Dest->Nodes.Add(Node);
			Node->Rename(nullptr, Dest);
			++OutResult.NodesMoved;
			PinRefreshTargets.Add(Entry.Key, Node);
			if (UEdGraphNode* Container = NodeByKey.FindRef(Entry.GraphCell)) { PinRefreshTargets.Add(Entry.GraphCell, Container); }
		}
	}

	// Pin refresh runs ONCE, after both creates and moves and before wiring, because wiring resolves pins that may
	// not exist until this runs. A container's outcome pins are DERIVED from the Exit nodes in its inner graph, so a
	// node created or moved INSIDE one changes what the container advertises - which is why the destination
	// containers are registered alongside the nodes themselves. Deeper-first ordering inside RefreshQuestNodePins means a
	// container is rebuilt after the inner nodes it reads.
	if (PinRefreshTargets.Num() > 0)
	{
		RefreshQuestNodePins(Bundle, NodeRowsByKey, PinRefreshTargets, SpawnWarnings);
	}
	OutResult.Skipped.Append(SpawnWarnings);

	// WIRING, after creation so both endpoints of a new relationship exist.
	{
		for (const FQuestDataEdge& Edge : Plan.RemovedEdges)
		{
			UEdGraphNode* const* FromNode = NodeByKey.Find(Edge.From);
			UEdGraphNode* const* ToNode   = NodeByKey.Find(Edge.To);
			if (!FromNode || !ToNode) { OutResult.Skipped.Add(FString::Printf(TEXT("removed edge %s -> %s: endpoint missing"), *Edge.From, *Edge.To)); continue; }

			UEdGraphPin* SourcePin = ResolveQuestSourcePin(*FromNode, Edge.Type);
			UEdGraphPin* DestPin   = SourcePin ? ResolveQuestDestPin(*ToNode, SourcePin->PinType.PinCategory) : nullptr;
			if (!SourcePin || !DestPin) { OutResult.Skipped.Add(FString::Printf(TEXT("removed edge %s -> %s: pin unresolved"), *Edge.From, *Edge.To)); continue; }

			// Break it ONLY when a direct link is what is actually there. The plan's edges are knot-collapsed, so this
			// relationship may run through reroute knots that fan out to other targets as well - breaking the first hop
			// would remove those relationships too, and deleting the chain would destroy routing placed deliberately.
			// Neither is derivable from the data, so a knot-routed removal is reported instead of guessed at.
			if (!SourcePin->LinkedTo.Contains(DestPin))
			{
				OutResult.Skipped.Add(FString::Printf(TEXT("removed edge %s %s %s is routed through reroute nodes - "
					"rewire it by hand, or the knots' other targets would go with it"), *Edge.From, *Edge.Type, *Edge.To));
				continue;
			}
			(*FromNode)->Modify();
			(*ToNode)->Modify();
			SourcePin->BreakLinkTo(DestPin);
			++OutResult.EdgesChanged;
		}

		for (const FQuestDataEdge& Edge : Plan.AddedEdges)
		{
			UEdGraphNode* const* FromNode = NodeByKey.Find(Edge.From);
			UEdGraphNode* const* ToNode   = NodeByKey.Find(Edge.To);
			if (!FromNode || !ToNode) { OutResult.Skipped.Add(FString::Printf(TEXT("added edge %s -> %s: endpoint missing"), *Edge.From, *Edge.To)); continue; }

			UEdGraphPin* SourcePin = ResolveQuestSourcePin(*FromNode, Edge.Type);
			UEdGraphPin* DestPin   = SourcePin ? ResolveQuestDestPin(*ToNode, SourcePin->PinType.PinCategory) : nullptr;
			if (!SourcePin || !DestPin) { OutResult.Skipped.Add(FString::Printf(TEXT("added edge %s -> %s: pin unresolved"), *Edge.From, *Edge.To)); continue; }
			if (SourcePin->LinkedTo.Contains(DestPin)) { continue; }   // already there; the comparison saw it through knots

			(*FromNode)->Modify();
			(*ToNode)->Modify();
			SourcePin->MakeLinkTo(DestPin);
			++OutResult.EdgesChanged;
		}
	}

	// DELETION LAST, and only when explicitly permitted. This is the one operation here that destroys authored work, so
	// it is opt-in twice: the plan reports an orphan whatever the setting, and removal happens only if asked. The plan has
	// already scoped orphans to levels the source DECLARES - a source describing one container says nothing about the
	// rest of the asset - and this honours that scoping rather than re-deriving it, which is where it would drift.
	if (Options.bDeleteOrphanedNodes)
	{
		for (const FQuestNodePlanEntry& Entry : Plan.Entries)
		{
			if (Entry.Action != EQuestNodePlanAction::Orphan) { continue; }

			UEdGraphNode* Node = NodeByKey.FindRef(Entry.Guid);
			if (!Node) { OutResult.Skipped.Add(FString::Printf(TEXT("orphan '%s' no longer resolves to a node"), *Entry.Key)); continue; }

			// A container carries its inner graph, so removing one takes everything inside it - nodes the plan counted as
			// UNTOUCHED rather than as orphans. Deleting a container by hand behaves the same way, so this is not refused,
			// but the blast radius has to be stated: a destructive step that under-reports what it removed is worse than
			// one that refuses.
			if (const UQuestlineNode_Quest* Quest = Cast<UQuestlineNode_Quest>(Node))
			{
				if (const UEdGraph* Inner = Quest->GetInnerGraph())
				{
					if (Inner->Nodes.Num() > 0)
					{
						OutResult.Skipped.Add(FString::Printf(TEXT("deleting container '%s' also removed %d node(s) inside it"),
							*Entry.Key, Inner->Nodes.Num()));
					}
				}
			}

			UEdGraph* OwningGraph = Node->GetGraph();
			if (!OwningGraph) { OutResult.Skipped.Add(FString::Printf(TEXT("orphan '%s' has no owning graph"), *Entry.Key)); continue; }

			OwningGraph->Modify();
			Node->Modify();
			Node->BreakAllNodeLinks();
			OwningGraph->RemoveNode(Node);
			++OutResult.NodesDeleted;
		}
	}

	for (const FQuestNodePlanEntry& Entry : Plan.Entries)
	{
		if (Entry.Action == EQuestNodePlanAction::Create) { continue; }   // already handled by the creation pass above
		if (Entry.Action == EQuestNodePlanAction::Orphan)
		{
			// Deferred only when deletion is not permitted. When it is, the deletion pass owns these - counting them here
			// as well would report the same node as both removed and left alone.
			if (!Options.bDeleteOrphanedNodes) { ++OutResult.EntriesDeferred; }
			continue;
		}
		if (Entry.Action != EQuestNodePlanAction::Update) { ++OutResult.EntriesDeferred; continue; }
		if (Entry.Changes.IsEmpty()) continue;

		UObject* Object = Entry.bIsQuestlineSelf ? static_cast<UObject*>(&Target) : NodeByKey.FindRef(Entry.Guid);
		if (!Object)
		{
			OutResult.Skipped.Add(FString::Printf(TEXT("entry '%s' no longer resolves to an object"), *Entry.Key));
			continue;
		}

		// CHILD TOPOLOGY BEFORE VALUES. ApplyQuestChangesToObject writes into objects that already exist; a change whose
		// Kind is ChildAdded names one that does not yet, and ChildRemoved names one that should stop existing.
		// Neither is a property write, which is why Kind was ignored here and those changes were planned, displayed,
		// and silently never performed.
		// ReattachInstanced is the operation, unchanged from the fresh path: it rebuilds a property's instanced
		// contents from the rows, and it ALREADY carries the contract the planner used to decide there was a change
		// at all - a property the source says nothing about is left alone, and one it describes is replaced
		// wholesale. Running it first means the value writes below land on the children that will actually survive.
		const bool bChildTopologyChanged = Entry.Changes.ContainsByPredicate([](const FQuestPropertyChange& C)
		{
			return C.Kind != EQuestPropertyChangeKind::Edit;
		});
		if (bChildTopologyChanged)
		{
			Object->Modify();
			TSet<FString> Consumed;
			TArray<FString> ChildWarnings;
			ReattachQuestInstancedChildren(Object, Entry.Key, Bundle, Consumed, ChildWarnings);
			OutResult.Skipped.Append(ChildWarnings);
			++OutResult.PropertiesWritten;   // the rebuild IS a write; counting zero would report a no-op run
		}

		// ONLY Edit changes are property writes. A topology change was already performed by the rebuild above, and
		// passing it here asks ApplyQuestChangesToObject to resolve a path naming a child the rebuild just created or
		// destroyed - which it correctly cannot, and reports as "names no property reachable". The change is not
		// unreachable; it was never a property write in the first place.
		TArray<FQuestPropertyChange> ValueChanges = Entry.Changes.FilterByPredicate([](const FQuestPropertyChange& C)
		{
			return C.Kind == EQuestPropertyChangeKind::Edit;
		});
		ApplyQuestChangesToObject(Object, Entry.Key, ValueChanges, OutResult.Skipped);
	}

	// ONE notify for the whole apply, rather than one per pass. Only AddNode / RemoveNode tell the editor anything on
	// their own - a direct Nodes-array mutation, a property write and MakeLinkTo / BreakLinkTo all leave it drawing
	// pre-apply state until something unrelated forces a redraw. Here rather than in the passes so every caller gets
	// it, the panel included, and no future pass has to remember. Skipped when nothing was written, so a no-op apply
	// stays genuinely inert.
	if (OutResult.ChangedAnything())
	{
		FSimpleQuestEditorUtilities::NotifyGraphAndDescendants(Target.QuestlineEdGraph);
	}
}