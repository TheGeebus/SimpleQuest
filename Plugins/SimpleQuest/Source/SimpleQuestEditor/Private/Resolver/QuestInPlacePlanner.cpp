// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestInPlacePlanner.h"

#include "Quests/QuestlineGraph.h"
#include "Resolver/QuestBundleDiff.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Resolver/QuestRowRestore.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"


void PlanQuestInPlace(const UQuestlineGraph& Target, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, const TArray<FString>& ReadWarnings, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies)
{
	OutPlan.Warnings = ReadWarnings;

	// The questline's own authored properties. A re-import writes these exactly as it writes a node's, so a plan that
	// describes only nodes would report an unchanged questline while a rename was pending.
	if (const FQuestDataTable* SelfTable = Bundle.TablesByType.Find(TEXT("questline_graph")))
	{
		if (SelfTable->Rows.Num() > 0)
		{
			FQuestNodePlanEntry SelfEntry;
			SelfEntry.Action           = EQuestNodePlanAction::Update;
			SelfEntry.bIsQuestlineSelf = true;
			SelfEntry.Key              = SelfTable->Rows[0].Key;
			SelfEntry.ClassName        = Target.GetClass()->GetName();
			SelfEntry.CurrentClassName = SelfEntry.ClassName;

			// A FABRICATED self row asserts nothing. The DataTable provenance has no self row, so one is invented from the
			// table's asset name purely to satisfy validation - comparing it would let a source that never described the
			// questline rename it. Entered anyway, so the plan still accounts for the questline, but with no changes.
			if (!Bundle.bSelfRowSynthesized)
			{
				DiffQuestObjectAgainstRow(&Target, SelfTable->Rows[0], FString(), SelfEntry, OutPlan, Policies);
				DiffQuestInstancedChildren(&Target, SelfTable->Rows[0].Key, Bundle, SelfEntry, OutPlan, Policies);
			}
			SelfEntry.Label = Target.GetName();
			OutPlan.Entries.Add(MoveTemp(SelfEntry));
		}
	}

	// Read the existing nodes under the SAME keying rule the spawn path writes: a studio-authored key when the node
	// carries one, else our GUID. That is the dual-key identity contract read in reverse.
	TMap<FString, FString> SourceKeyByGuid;
	TMap<FString, const UQuestlineNodeBase*> NodeByGuid;
	TMap<FString, FString> GraphCellByGuid;
	CollectQuestNodeIdentity(Target.QuestlineEdGraph, SourceKeyByGuid, NodeByGuid, &GraphCellByGuid);

	TArray<FString> AllGuids;
	NodeByGuid.GetKeys(AllGuids);
	TMap<FString, FString> GuidByKey;
	BuildQuestNodeKeyIndex(SourceKeyByGuid, AllGuids, GuidByKey, OutPlan.AmbiguousKeys);

	// A level cell names a container by KEY; a reader wants its NAME. Declared here rather than as a free function
	// because it needs the identity maps this walk just built, and it is nobody else's business.
	auto LevelDisplayName = [&NodeByGuid, &GuidByKey](const FString& Cell) -> FString
	{
		if (Cell.IsEmpty() || Cell == TEXT("root")) { return TEXT("root"); }
		const FString* Guid = GuidByKey.Find(Cell);
		const UQuestlineNodeBase* Node = Guid ? NodeByGuid.FindRef(*Guid) : NodeByGuid.FindRef(Cell);
		return Node ? Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() : Cell;
	};

	// Names for every live node, keyed the way rows and edges address them. Done once here rather than per consumer,
	// because resolving a key to a title needs the identity maps and the panel has neither.
	for (const TPair<FString, const UQuestlineNodeBase*>& Pair : NodeByGuid)
	{
		if (!Pair.Value) { continue; }
		const FString Title = Pair.Value->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		OutPlan.LabelByKey.Add(Pair.Key, Title);
		OutPlan.LabelByKey.Add(QuestNodeIdentityKey(Pair.Key, SourceKeyByGuid), Title);
	}

	for (const FString& Key : OutPlan.AmbiguousKeys)
	{
		OutPlan.Warnings.Add(FString::Printf(TEXT("'%s' names more than one node - that row is not planned, and neither "
			"claimant is treated as an orphan. Clear the import provenance on the duplicate to resolve it."), *Key));
	}

	// The levels this source is talking about, in the one namespace. Anything outside them is none of its business.
	TSet<FString> DeclaredLevels;
	for (const TPair<FString, const FQuestDataRow*>& Pair : NodeRowsByKey)
	{
		DeclaredLevels.Add(ResolveQuestLevelToGuid(Pair.Value->Get(TEXT("graph")), GuidByKey));
	}

	TSet<FString> MatchedGuids;
	for (const TPair<FString, const FQuestDataRow*>& Pair : NodeRowsByKey)
	{
		const FQuestDataRow& Row = *Pair.Value;

		FQuestNodePlanEntry Entry;
		Entry.Key			= Row.Key;
		Entry.ClassName		= Row.Get(TEXT("class"));
		Entry.GraphCell		= Row.Get(TEXT("graph"));
		Entry.GraphLabel	= LevelDisplayName(Entry.GraphCell);

		const FString* FoundGuid = GuidByKey.Find(Row.Key);
		const UQuestlineNodeBase* Node = FoundGuid ? NodeByGuid.FindRef(*FoundGuid) : nullptr;
		if (!Node)
		{
			// Only promise what the spawn path can deliver. A class no loaded module provides, or a level nothing declares,
			// both make SpawnQuestNodeFromRow / ImportQuestGraphLevel skip the row - so planning a CREATE would report work that
			// silently never happens, which is worse than reporting nothing.
			if (!ResolveQuestBundleClass(Entry.ClassName))
			{
				OutPlan.Refusals.Add(FString::Printf(TEXT("row '%s' names class '%s', which no loaded module provides - "
					"it cannot be created"), *Row.Key, *Entry.ClassName));
				continue;
			}
			const FString Level = ResolveQuestLevelToGuid(Entry.GraphCell, GuidByKey);
			const bool bLevelExists = Level.IsEmpty() || Level == TEXT("root")
				|| GuidByKey.Contains(Level) || NodeRowsByKey.Contains(Entry.GraphCell);
			if (!bLevelExists)
			{
				OutPlan.Refusals.Add(FString::Printf(TEXT("row '%s' sits in level '%s', which no node or row declares - "
					"it cannot be reached"), *Row.Key, *Entry.GraphCell));
				continue;
			}

			Entry.Action = EQuestNodePlanAction::Create;
			// A create has nothing to ask, so this is whatever the source volunteered - often nothing, which is honest.
			Entry.Label = Row.Get(TEXT("NodeLabel"));
			OutPlan.Entries.Add(MoveTemp(Entry));
			continue;
		}

		MatchedGuids.Add(*FoundGuid);
		Entry.Action			= EQuestNodePlanAction::Update;
		Entry.Guid				= *FoundGuid;
		Entry.CurrentClassName	= Node->GetClass()->GetName();
		Entry.CurrentGraphCell  = GraphCellByGuid.FindRef(*FoundGuid);
		Entry.CurrentGraphLabel = LevelDisplayName(Entry.CurrentGraphCell);

		// A class difference is not a change to this node - it says the row describes a DIFFERENT node. A Step is not a
		// mutated Quest, and nothing about one instance can be carried into the other, so there is nothing to apply and
		// no identity to preserve. A source that genuinely means "replace this" says so by giving the row a NEW KEY,
		// which orphans the old node and creates a new one with a new identity - already supported, and the honest
		// expression of the intent. Refused after the match above is recorded, so a refused row does not ALSO report
		// its node as an orphan.
		if (Entry.ClassName != Entry.CurrentClassName)
		{
			OutPlan.Refusals.Add(FString::Printf(TEXT("row '%s' names class '%s' but matches a node of class '%s'. A node "
				"cannot change class - to replace it, give the row a new key so the old node orphans and a new one is created"),
				*Row.Key, *Entry.ClassName, *Entry.CurrentClassName));
			continue;
		}

		// Compare levels in ONE namespace. The two sides spell a level differently by design - the writer names it by the
		// container's GUID, the asset walk by whatever key that container answers to - so a raw compare would report every
		// node inside a semantically-keyed container as having moved when it has not.
		const FString IncomingLevel = ResolveQuestLevelToGuid(Entry.GraphCell, GuidByKey);
		const FString CurrentLevel  = ResolveQuestLevelToGuid(Entry.CurrentGraphCell, GuidByKey);
		Entry.bMoved = (IncomingLevel != CurrentLevel);

		DiffQuestObjectAgainstRow(Node, Row, FString(), Entry, OutPlan, Policies);
		DiffQuestInstancedChildren(Node, Row.Key, Bundle, Entry, OutPlan, Policies);

		// FullTitle, not ListView: ListView is a palette context, and a node that distinguishes them answers it with its
		// TYPE label - which would give every placement of that class the same name to sort and display by.
		Entry.Label = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		OutPlan.Entries.Add(MoveTemp(Entry));
	}

	// Anything the asset holds that the source no longer mentions - but ONLY within the levels the source actually
	// declares. A source describing one container says nothing about the rest of the asset, and treating its silence as
	// deletion is what would make a delete-orphans policy unsafe to offer at all.
	for (const TPair<FString, const UQuestlineNodeBase*>& Pair : NodeByGuid)
	{
		if (MatchedGuids.Contains(Pair.Key)) continue;

		const FString NodeLevel = ResolveQuestLevelToGuid(GraphCellByGuid.FindRef(Pair.Key), GuidByKey);
		if (!DeclaredLevels.Contains(NodeLevel)) { ++OutPlan.UntouchedNodeCount; continue; }

		// A contested key names two nodes; neither is a row's target, and neither may be deleted on that basis.
		if (OutPlan.AmbiguousKeys.Contains(QuestNodeIdentityKey(Pair.Key, SourceKeyByGuid))) continue;

		FQuestNodePlanEntry Entry;
		Entry.Action			= EQuestNodePlanAction::Orphan;
		Entry.Key				= QuestNodeIdentityKey(Pair.Key, SourceKeyByGuid);
		Entry.Guid				= Pair.Key;
		Entry.ClassName			= Pair.Value->GetClass()->GetName();
		Entry.CurrentClassName	= Entry.ClassName;
		Entry.CurrentGraphCell  = GraphCellByGuid.FindRef(Pair.Key);
		Entry.CurrentGraphLabel = LevelDisplayName(Entry.CurrentGraphCell);
		Entry.Label				= Pair.Value->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		OutPlan.Entries.Add(MoveTemp(Entry));
	}

	// Wiring. Derived from the live graph with the SAME walk the writer uses, so an unchanged questline compares clean
	// rather than looking rewired. Without this a source that only re-points a "next" column reports no change at all,
	// and a synthesized gate appears as a CREATE while the edges that make it function stay invisible.
	{
		const TUniquePtr<FQuestlineGraphTraversalPolicy> Policy = MakeUnique<FQuestlineGraphTraversalPolicy>();
		TArray<FQuestDataEdge> LiveEdges;
		for (const TPair<FString, const UQuestlineNodeBase*>& Pair : NodeByGuid)
		{
			CollectQuestWireEdges(Pair.Value, *Policy, LiveEdges);
		}
		CompareQuestEdges(Bundle.Edges, LiveEdges, GuidByKey, NodeByGuid, OutPlan.AddedEdges, OutPlan.RemovedEdges);
	}

	/**
	 * WIRING MUST NOT CROSS A CONTAINER BOUNDARY. A link joins two nodes in ONE graph - a designer cannot author one
	 * that leaves a container, because authoring happens inside a single graph at a time. The compiler TOLERATES such
	 * a link (it mints a context alias to route it), so nothing downstream complains and a source could quietly
	 * produce a topology nobody can open and edit. That silence is why this is refused here rather than left to fail
	 * later. Not move-specific: a hand-written source declaring the same edge is the same defect.
	 * Containment is exempt - contains(InnerGraph) names a container and a node INSIDE it, so differing levels is its
	 * definition rather than a fault.
	 * Bundle.Edges is the POST-APPLY wiring set: anything live it omits is already planned as a removal, so comparing
	 * against it describes the graph the source is asking for rather than the one that exists now.
	 */
	{
		auto LevelAfterApply = [&NodeRowsByKey, &GuidByKey, &GraphCellByGuid](const FString& Key) -> FString
		{
			if (const FQuestDataRow* const* Row = NodeRowsByKey.Find(Key))
			{
				return ResolveQuestLevelToGuid((*Row)->Get(TEXT("graph")), GuidByKey);
			}
			const FString* Guid = GuidByKey.Find(Key);
			return Guid ? ResolveQuestLevelToGuid(GraphCellByGuid.FindRef(*Guid), GuidByKey) : FString();
		};

		for (const FQuestDataEdge& E : Bundle.Edges)
		{
			if (E.Type.StartsWith(TEXT("contains("))) { continue; }

			const FString FromLevel = LevelAfterApply(E.From);
			const FString ToLevel   = LevelAfterApply(E.To);
			// An endpoint that resolves to nothing is not this check's business - a missing node is reported by the
			// passes that own it, and guessing here would refuse a row for a reason that is not its own.
			if (FromLevel.IsEmpty() || ToLevel.IsEmpty() || FromLevel == ToLevel) { continue; }

			OutPlan.Refusals.Add(FString::Printf(TEXT("edge '%s' %s '%s' would cross a container boundary - '%s' ends up in "
				"level '%s' while '%s' is in level '%s'. A link can only join two nodes in the same graph, so this is a "
				"topology the editor cannot author"),
				*E.From,
				*E.Type,
				*E.To,
				*E.From,
				*FromLevel,
				*E.To,
				*ToLevel));
		}
	}

	/**
	 * Deterministic order, because everything above walks TMaps and two identical plans would otherwise render differently
	 * - which makes a panel feel broken and makes two screenshots impossible to compare. The questline itself leads, then
	 * by action so like sits with like, then by name. A panel re-sorts on top of this; it only needs a stable start.
	 */
	OutPlan.Entries.Sort([](const FQuestNodePlanEntry& A, const FQuestNodePlanEntry& B)
	{
		if (A.bIsQuestlineSelf != B.bIsQuestlineSelf) { return A.bIsQuestlineSelf; }
		if (A.Action != B.Action) { return static_cast<uint8>(A.Action) < static_cast<uint8>(B.Action); }
		const FString& AName = A.Label.IsEmpty() ? A.Key : A.Label;
		const FString& BName = B.Label.IsEmpty() ? B.Key : B.Label;
		// Natural order, so "Chapter 10" follows "Chapter 9" instead of "Chapter 1". Authored labels routinely end in a
		// number, and that is exactly the case a plain lexicographic sort scatters worst.
		const int32 ByName = UE::ComparisonUtility::CompareNaturalOrder(AName, BName);
		return ByName == 0 ? A.Key < B.Key : ByName < 0;   // key breaks ties, so duplicates never swap
	});
	for (FQuestNodePlanEntry& Entry : OutPlan.Entries)
	{
		Entry.Changes.Sort([](const FQuestPropertyChange& A, const FQuestPropertyChange& B) { return A.Property < B.Property; });
	}
}

