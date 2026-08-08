// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// PROTOTYPE - Resolver, Phase 2 import (the round-trip's second half). Reconstructs AUTHORED editor nodes from the
// interlingua table folder an export produced, then feeds the EXISTING compiler - never reverses the compiler. Creates
// a FRESH asset (QuestlineID suffixed _RT so its compiled tag namespace doesn't collide with the original), so the
// round-trip is verifiable by the two oracles: C (re-export this asset, diff the folders modulo _RT) and B2
// (compile + DumpCompiled both, diff modulo the tag prefix). Console-triggered, editor-only. Not shipped API.

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/DataTable.h"
#include "Factories/QuestlineGraphFactory.h"
#include "Graph/QuestlineGraphSchema.h"
#include "IAssetTools.h"
#include "Internationalization/Text.h"
#include "ISimpleQuestEditorModule.h"
#include "Misc/Paths.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Quests/QuestlineGraph.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestBundleDiff.h"
#include "Resolver/QuestBundleTransforms.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestDataValueBuilder.h"
#include "Resolver/QuestFlowConventions.h"
#include "Resolver/QuestGraphBuilder.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestImportOperations.h"
#include "Resolver/QuestInPlacePlan.h"
#include "Resolver/QuestInstancedChildren.h"
#include "Resolver/QuestMappingSource.h"
#include "Resolver/QuestNodeIdentity.h"
#include "Resolver/QuestPlanBroker.h"
#include "Resolver/QuestReflectionUtils.h"
#include "Resolver/QuestRowRestore.h"
#include "SimpleQuestLog.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Utilities/QuestlineGraphCompiler.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	// The import routing core speaks the shared, format-free FQuestDataBundle (Resolver/QuestDataBundle.h). The local
	// FImport* bundle structs + the TSV parsing (Unsanitize / ParseTable / ParseEdges) moved to the TSV provider
	// (Resolver/TsvQuestDataFormat::ReadBundle) in Stage 2 - the routing core never touches a file or format. What was
	// ReadAndValidate splits: the provider reads the folder into a bundle; ValidateBundle (below) does the structural
	// checks on that already-parsed bundle.

	// P0 (routing half) - structural validation of an ALREADY-READ bundle. File reading (folder discovery + TSV parse)
	// is the provider's job (ReadBundle); this does ONLY the structural checks and builds the two lookup indices the
	// later phases need. Refuse (return false) on any inconsistency so no partial asset is ever created (validate-
	// upfront). Provider-agnostic: a malformed bundle from ANY format provider is refused here identically.
	bool ValidateBundle(const FQuestDataBundle& Bundle,
	                    TMap<FString, const FQuestDataRow*>& NodeRowsByKey,   // node/self key -> row (excludes child rows)
	                    TSet<FString>& AllRowKeys,                            // every key incl. instanced child keys
	                    FString& OutError)
	{
		// The questline-self table is keyed "questline_graph" - required, exactly one row.
		const FQuestDataTable* Questline = Bundle.TablesByType.Find(TEXT("questline_graph"));
		if (!Questline) { OutError = TEXT("no questline_graph table (the self row)"); return false; }
		if (Questline->Rows.Num() != 1) { OutError = TEXT("questline_graph table must have exactly one row"); return false; }

		// Index every row key. Node/self rows are keyed by GUID digits or the EffectiveID; instanced child rows carry
		// a '/' path segment. Only NODE rows spawn editor nodes, so split the two - but track ALL keys so edge
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

		// Validate exactly one Entry row per graph cell (each graph level has one Entry - the import adopts it).
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

	// ---- MAPPING (studio source-shape translation) ----------------------------------------------------------------
	// A studio's own-shaped source (usually a flat table whose rows are different node kinds, distinguished by a "type"
	// column) arrives as one table (ReadQuestBundle keys tables by file). This makes it routable: read the discriminator
	// column, look up each row's node class, stamp the row's "class" cell, then rename bound source columns to their
	// canonical property names - applying a binding only where the resolved class actually has that property (bind once,
	// land where it fits). Runs before ApplyQuestFlowConventions/ValidateBundle so the class-driven pipeline sees canonical
	// rows. The questline_graph self table is left untouched (it isn't a fanned-out source table).
	bool ApplyMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
	{
		if (Mapping.DiscriminatorColumn.IsNone())
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: mapping has no discriminator column set - malformed mapping, refusing."));
			return false;
		}
		const FString DiscCol = Mapping.DiscriminatorColumn.ToString();

		// Extract the actual source shape from the bundle we're about to transform (no drift - this IS the data), then run
		// the shared guard. BINDING: refuse the whole import on any failure rather than silently drop rows. The extraction
		// must read the discriminator cell BEFORE the routing loop removes it (below).
		TArray<FName> ActualColumns;
		TArray<FString> ActualDiscriminatorValues;
		{
			TSet<FName> ColSet; TSet<FString> ValSet;
			for (const TPair<FString, FQuestDataTable>& TP : Bundle.TablesByType)
			{
				if (TP.Key == TEXT("questline_graph")) continue;
				for (const FString& C : TP.Value.Columns) ColSet.Add(FName(*C));
				for (const FQuestDataRow& R : TP.Value.Rows)
				{
					const FString V = R.Get(DiscCol);
					if (!V.IsEmpty()) ValSet.Add(V);
				}
			}
			ActualColumns = ColSet.Array();
			ActualDiscriminatorValues = ValSet.Array();
		}
		TArray<FText> GuardErrors;
		TArray<FText> GuardWarnings;
		const bool bGuardOK = ValidateMappingAgainstSource(Mapping, ActualColumns, ActualDiscriminatorValues, GuardErrors, &GuardWarnings);
		for (const FText& W : GuardWarnings)   // advisories log regardless of pass/fail - they never block, only inform
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline mapping advisory: %s"), *W.ToString());
		if (!bGuardOK)
		{
			for (const FText& E : GuardErrors)
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline mapping guard: %s"), *E.ToString());
			return false;   // refuse - no partial asset from an unsafe mapping
		}

		// The routing class map - the SAME shared builder the guard used, so membership can't drift. The guard already
		// refused on any build error, so BuildErrors can't fire here. Then cache each class's authored-property set for the
		// per-row bind-where-it-fits check.
		TMap<FString, UClass*> ClassByValue;   // keyed by NormalizeDiscriminatorValue
		TArray<FText> BuildErrors;
		BuildDiscriminatorClassMap(Mapping, ClassByValue, BuildErrors);
		TMap<UClass*, TSet<FName>> PropsByClass;
		for (const TPair<FString, UClass*>& CV : ClassByValue)
		{
			if (!PropsByClass.Contains(CV.Value))
			{
				TSet<FName> Names;
				for (const FName& N : GetAuthoredPropertyNames(CV.Value)) Names.Add(N);
				PropsByClass.Add(CV.Value, MoveTemp(Names));
			}
		}

		int32 Routed = 0;
		for (TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
		{
			if (TablePair.Key == TEXT("questline_graph")) continue;   // self row is not a fanned-out source table

			for (FQuestDataRow& Row : TablePair.Value.Rows)
			{
				// 1. Which kind is this row? Read the discriminator cell, resolve its class, stamp the "class" cell.
				const FString DiscValue = Row.Get(DiscCol);
				UClass* const* Found = ClassByValue.Find(NormalizeDiscriminatorValue(DiscValue));
				if (!Found)
				{
					// Unreachable at import (the guard refused any unmapped value); defensive for a guard-less caller.
					Warnings.Add(FString::Printf(TEXT("mapping: row '%s' has %s='%s' with no class mapping - row not routed"),
						*Row.Key, *DiscCol, *DiscValue));
					continue;
				}
				UClass* RowClass = *Found;
				Row.Cells.Add(TEXT("class"), FQuestDataValue::MakeString(RowClass->GetName()));   // short name (TryFindTypeSlow form)
				Row.Cells.Remove(DiscCol);   // the discriminator column isn't a node property
				const TSet<FName>& RowProps = PropsByClass[RowClass];

				// 2. Rename each bound source column to its canonical property name - but only when this class has that
				//    property (bind once, land where it fits). A binding that doesn't fit this class is simply skipped.
				for (const FQuestColumnBinding& B : Mapping.Bindings)
				{
					if (!RowProps.Contains(B.TargetProperty)) continue;   // property not on this class - binding doesn't apply
					const FString SrcCol = B.SourceColumn.ToString();
					FQuestDataValue* Cell = Row.Cells.Find(SrcCol);
					if (!Cell) continue;   // source column absent on this row - the absent-policy is handled downstream
					FQuestDataValue Moved = MoveTemp(*Cell);
					Row.Cells.Remove(SrcCol);
					Row.Cells.Add(B.TargetProperty.ToString(), MoveTemp(Moved));
				}
				++Routed;
			}
		}
		if (Routed > 0)
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: mapping routed + renamed %d row(s)."), Routed);
		return true;
	}

	// ---- WIRE BINDINGS (studio-declared row-adjacent relationships) ------------------------------------------------
	// A studio's flat source expresses flow as a COLUMN of target keys ("next": n_exit) rather than an edge table. Each
	// wire-binding names such a column plus the edge kind it means; we synthesize the identical {From,Type,To} edges the
	// edge table would have carried, then STRIP the cell so it never reaches RestoreQuestCell as a bogus property (same
	// contract as the flow conventions below). An EMPTY qualifier emits "verb()", which ResolveSourcePin reads as "this
	// node's default output of that kind" - so one binding wires Entry, Step and Exit alike. Mapping-only: it's studio
	// vocabulary translation, so it never runs on our own round-trip.
	void ApplyWireBindings(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
	{
		if (Mapping.WireBindings.Num() == 0) return;

		int32 Synthesized = 0;
		for (TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
		{
			if (TablePair.Key == TEXT("questline_graph")) continue;   // the self row wires nothing

			for (FQuestDataRow& Row : TablePair.Value.Rows)
			{
				for (const FQuestWireBinding& Wire : Mapping.WireBindings)
				{
					if (Wire.SourceColumn.IsNone()) continue;
					const FString Col = Wire.SourceColumn.ToString();
					const FQuestDataValue* Cell = Row.Cells.Find(Col);
					if (!Cell) continue;                                   // column absent on this row - nothing to wire

					const TArray<FString> Targets = ParseQuestKeyList(*Cell);
					Row.Cells.Remove(Col);                                 // strip: a wire column is not a node property
					if (Targets.Num() == 0) continue;                      // present but empty == no wiring, not an error

					const FString EdgeType = FString::Printf(TEXT("%s(%s)"), *Wire.EdgeVerb.ToString(), *Wire.Qualifier);
					for (const FString& Target : Targets)
					{
						Bundle.Edges.Add({ Row.Key, EdgeType, Target });
						++Synthesized;
					}
				}
			}
		}
		if (Synthesized > 0)
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: synthesized %d edge(s) from wire binding(s)."), Synthesized);
	}



	// ---- In-place planning -----------------------------------------------------------------------------------------
	// Compare a validated bundle against an EXISTING asset and describe what a re-import would do, changing nothing.

	// A console-typed asset path is usually the short form "/Game/Path/Asset"; FSoftObjectPath needs "/Game/Path/Asset.Asset".
	FString NormalizeConsoleAssetPath(const FString& In)
	{
		if (In.Contains(TEXT("."))) return In;
		FString Ignored, AssetName;
		if (In.Split(TEXT("/"), &Ignored, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return In + TEXT(".") + AssetName;
		}
		return In;
	}

	void PlanInPlace(const UQuestlineGraph& Target, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, const TArray<FString>& ReadWarnings, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies = FQuestAbsentPolicyResolver())
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
				// both make SpawnNodeFromRow / ImportGraphLevel skip the row - so planning a CREATE would report work that
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
			CompareQuestEdges(Bundle.Edges, LiveEdges, GuidByKey, OutPlan.AddedEdges, OutPlan.RemovedEdges);
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

	const TCHAR* PlanActionName(EQuestNodePlanAction Action)
	{
		switch (Action)
		{
		case EQuestNodePlanAction::Create: return TEXT("CREATE");
		case EQuestNodePlanAction::Orphan: return TEXT("ORPHAN");
		default:                           return TEXT("UPDATE");
		}
	}

	void LogInPlacePlan(const FQuestInPlacePlan& Plan)
	{
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: in-place PLAN for '%s' - %d update(s) (%d with changes), %d create(s), %d orphan(s), "
			"%d node(s) outside the levels this source declares, %d contested key(s), %d wire edge(s) added, %d removed. Nothing was modified."),
			*Plan.TargetAssetPath,
			Plan.CountOf(EQuestNodePlanAction::Update),
			Plan.ChangedNodeCount(),
			Plan.CountOf(EQuestNodePlanAction::Create),
			Plan.CountOf(EQuestNodePlanAction::Orphan),
			Plan.UntouchedNodeCount,
			Plan.AmbiguousKeys.Num(),
			Plan.AddedEdges.Num(),
			Plan.RemovedEdges.Num());

		for (const FQuestDataEdge& E : Plan.RemovedEdges) { UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [WIRE-] %s|%s|%s"), *E.From, *E.Type, *E.To); }
		for (const FQuestDataEdge& E : Plan.AddedEdges)   { UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [WIRE+] %s|%s|%s"), *E.From, *E.Type, *E.To); }
		for (const FString& R : Plan.Refusals) { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("  [REFUSED] %s"), *R); }

		for (const FQuestNodePlanEntry& Entry : Plan.Entries)
		{
			// Unchanged matches are the common case on a healthy re-import; listing them would bury the ones that matter.
			if (Entry.Action == EQuestNodePlanAction::Update && Entry.Changes.Num() == 0 && !Entry.bMoved) continue;
			
			// An orphan has no incoming row, so its level is only known from the asset side; the questline itself sits in no
			// level at all, being the thing levels belong to.
			const FString& Level = (Entry.Action == EQuestNodePlanAction::Orphan) ? Entry.CurrentGraphCell : Entry.GraphCell;
			const FString Where = Entry.bIsQuestlineSelf ? FString(TEXT("the questline itself")) : FString::Printf(TEXT("graph '%s'"), *Level);
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [%s] %s (%s) - %s%s"),
				PlanActionName(Entry.Action),
				*Entry.Key,
				*Entry.ClassName,
				*Where,
				Entry.bMoved ? TEXT("  ** moves to a different container **") : TEXT(""));

			if (Entry.bMoved)
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("      graph: %s -> %s"), *Entry.CurrentGraphCell, *Entry.GraphCell);
			}
			for (const FQuestPropertyChange& Change : Entry.Changes)
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("      %s: '%s' -> '%s'"), *Change.Property, *Change.CurrentText, *Change.IncomingText);
			}
		}

		for (const FString& W : Plan.Warnings) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: %s"), *W);
		if (Plan.IsNoOp())
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: the asset already matches the source - a re-import would change nothing."));
		}
	}

	void ImportQuestlineCmd(const TArray<FString>& Args)
	{
		// Separate the optional "--format=<name>" arg from the positional path args BEFORE rejoining (it must not get
		// swept into the space-rejoined folder path). PathArgs = every arg that isn't a --flag.
		// --datatable=<AssetPath> selects the asset provenance; then no source FOLDER is needed (the dest path is the only
		// positional arg). Otherwise the folder is the positional args before the dest, space-rejoined.
		FString DataTablePath;
		FString InPlacePath;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--datatable="))) DataTablePath = Arg.RightChop(12);
			else if (Arg.StartsWith(TEXT("--in-place="))) InPlacePath = Arg.RightChop(11);
		}
		const bool bInPlace = !InPlacePath.IsEmpty();
		const bool bApply = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--apply"), ESearchCase::IgnoreCase); });
		const bool bResetAbsent = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--reset-absent"), ESearchCase::IgnoreCase); });
		const bool bDeleteOrphans = Args.ContainsByPredicate([](const FString& A){ return A.Equals(TEXT("--delete-orphans"), ESearchCase::IgnoreCase); });

		TArray<FString> PathArgs;
		for (const FString& Arg : Args)
		{
			if (!Arg.StartsWith(TEXT("--"))) PathArgs.Add(Arg);
		}
		// A source folder unless the DataTable provenance supplies it, plus a dest package unless --in-place names the target.
		const int32 MinPositional = (DataTablePath.IsEmpty() ? 1 : 0) + (bInPlace ? 0 : 1);
		if (PathArgs.Num() < MinPositional)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: usage 'SimpleQuest.ImportQuestline <FolderPath> <DestPackagePath> [--format=json] [--mapping=<asset>]' "
				"or 'SimpleQuest.ImportQuestline <DestPackagePath> --datatable=<asset> [--mapping=<asset>]'. "
				"Add '--in-place=<AssetPath>' to compare against an existing asset instead of creating one; the dest package arg is then omitted."));
			return;
		}

		// --in-place takes no dest package arg, but the create form does - so adapting one command into the other easily
		// leaves the dest behind. Space-rejoining would swallow it into the folder path, and the only symptom would be a
		// "folder not found" naming a path the caller never typed. Name the actual mistake instead. Guarded on there being
		// more than one positional arg so a genuine root-anchored source folder is never mistaken for a package path.
		if (bInPlace && PathArgs.Num() > 1 && FPackageName::IsValidLongPackageName(PathArgs.Last()))
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: trailing argument '%s' is a package path, which --in-place does not take - the "
				"target is named by --in-place=<AssetPath>. Pass only the source folder. Nothing was modified."), *PathArgs.Last());
			return;
		}

		// Console arg tokenization splits on whitespace and does NOT honor quotes, so a folder path containing spaces
		// (e.g. "E:/Unreal Projects/...") arrives as multiple Args. The dest package path is the LAST path arg (never has
		// spaces - it's a /Game/... mount path); the folder path is everything before it, rejoined with spaces. With
		// --in-place there is no dest arg to peel off, so the whole positional remainder is the folder.
		const FString DestPackagePath = bInPlace ? FString() : PathArgs.Last();
		FString FolderPath;
		if (DataTablePath.IsEmpty())
		{
			TArray<FString> FolderParts = PathArgs;
			if (!bInPlace) FolderParts.Pop();                 // drop the dest path
			FolderPath = FString::Join(FolderParts, TEXT(" "));
			FolderPath = FolderPath.TrimQuotes();             // tolerate quotes if the caller added them
		}

		// The source is an ENDPOINT: a file folder read through a format provider, or a DataTable asset. One read call, so
		// the import path never branches on provenance again.
		FQuestDataEndpoint Endpoint;
		if (!DataTablePath.IsEmpty())
		{
			// A console-typed asset path is usually the short form; normalize so --datatable accepts the same form --mapping does.
			Endpoint.Kind = EQuestEndpointKind::DataTable;
			Endpoint.Table = TSoftObjectPtr<UDataTable>(FSoftObjectPath(NormalizeConsoleAssetPath(DataTablePath)));
		}
		else
		{
			const TUniquePtr<ISimpleQuestDataFormat> Format = MakeQuestDataFormat(Args, TEXT("ImportQuestline"));
			if (!Format)
			{
				return;   // the unregistered-format error was already logged; refuse before creating anything.
			}
			Endpoint.Kind = EQuestEndpointKind::ForeignFile;
			Endpoint.FormatName = Format->FormatName();
			Endpoint.Folder = FolderPath;
		}

		// Read, translate and validate in one call, shared with the in-place branch below so the pipeline has a single
		// definition. The three failure modes still read differently because the operation phrases its own error.
		FQuestDataBundle Bundle;
		TMap<FString, const FQuestDataRow*> NodeRowsByKey;
		TSet<FString> AllRowKeys;
		TArray<FString> Warnings;
		FString ReadError;
		if (!QuestImport_ReadAndValidate(Endpoint, LoadQuestMappingArg(Args), Bundle, NodeRowsByKey, AllRowKeys, Warnings, ReadError))
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: %s. No asset created."), *ReadError);
			return;
		}

		// In-place: describe what a re-import WOULD do to the existing asset, then stop. Planning is read-only and is the
		// only thing --in-place does; applying a plan is a separate, explicitly-requested step. That ordering means a
		// mistyped target path can never damage an asset. The plan is produced as data rather than only logged, so the
		// editor action can render exactly what the console prints.
		if (bInPlace)
		{
			const FString AssetPath = NormalizeConsoleAssetPath(InPlacePath);
			UQuestlineGraph* TargetGraph = Cast<UQuestlineGraph>(FSoftObjectPath(AssetPath).TryLoad());
			if (!TargetGraph || !TargetGraph->QuestlineEdGraph)
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: --in-place target '%s' did not load as a questline graph. Nothing was modified."), *AssetPath);
				return;
			}

			FQuestInPlacePlan Plan;
			FQuestImportRequest Request;
			Request.Endpoint = Endpoint;
			Request.Mapping = LoadQuestMappingArg(Args);
			Request.bDeleteOrphans = bDeleteOrphans;
			// Resolved out here rather than inside the run, because the mode has to be REPORTED before any work happens:
			// a plan is only interpretable against the policy that produced it, and the same source and asset yield
			// different plans under Preserve and Reset.
			Request.Policies = QuestImport_ResolvePolicies(Request.Mapping, bResetAbsent);

			const TCHAR* PolicyName =
				Request.Policies.Default == EQuestAbsentFieldPolicy::Reset   ? TEXT("Reset") :
				Request.Policies.Default == EQuestAbsentFieldPolicy::Require ? TEXT("Require") : TEXT("Preserve");
			const bool bWouldDeleteOrphans =
				bDeleteOrphans || (Request.Mapping && Request.Mapping->bDeleteOrphanedNodes);
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: in-place %s - source '%s', absent-field policy %s%s.%s"),
				bApply ? TEXT("APPLY") : TEXT("PLAN (read-only)"),
				DataTablePath.IsEmpty() ? *FolderPath : *DataTablePath,
				PolicyName,
				bResetAbsent ? TEXT(" (via --reset-absent)") : TEXT(""),
				bApply && bWouldDeleteOrphans ? TEXT(" [WILL DELETE ORPHANS]") : TEXT(""));

			// The transaction wraps the call because the run applies internally. A plan carrying refusals applies nothing,
			// so on that path this opens and closes with no object recorded - which the transaction buffer discards.
			TUniquePtr<FScopedTransaction> Transaction;
			if (bApply)
			{
				Transaction = MakeUnique<FScopedTransaction>(
					NSLOCTEXT("SimpleQuestEditor", "ApplyInPlaceImport", "Apply In-Place Import"));
			}

			FQuestImportOutcome Outcome;
			if (!QuestImport_RunInPlace(*TargetGraph, Request, bApply, Outcome))
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: %s. Nothing was modified."), *Outcome.Error);
				return;
			}

			Outcome.Plan.TargetAssetPath = AssetPath;
			LogInPlacePlan(Outcome.Plan);
			// The log is one rendering of the plan; the panel is another. Published unconditionally, including for a plan
			// about to be applied, so the panel always shows what the run actually decided.
			FQuestPlanSource PlanSource;
			PlanSource.Folder     = Endpoint.Folder;
			PlanSource.FormatName = Endpoint.FormatName;
			PlanSource.Table      = Endpoint.Table.ToSoftObjectPath();
			FQuestPlanBroker::Get().Publish(Outcome.Plan.TargetAssetPath, Outcome.Plan, PlanSource);

			if (!bApply)
			{
				return;   // planning is the default; mutating is opted into
			}

			// A plan carrying refusals or contested keys is not trustworthy in ANY part - those say the planner could not
			// describe the source, not merely that one row is odd. ApplyPlan already declined; this reports why.
			if (Outcome.ApplyResult.bRefused)
			{
				UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: --apply refused - the plan carries %d refusal(s) and %d contested key(s). "
					"Resolve those and re-plan. Nothing was modified."),
					Outcome.Plan.Refusals.Num(),
					Outcome.Plan.AmbiguousKeys.Num());
				return;
			}

			const FQuestApplyResult& Result = Outcome.ApplyResult;

			for (const FString& S : Result.Skipped) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: apply skipped %s"), *S);
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: APPLIED to '%s' - %d property change(s), %d node(s) created, "
				"%d node(s) moved, %d wire edge(s) changed, %d node(s) DELETED. %d entry/entries deferred by policy, %d skipped."),
				*AssetPath,
				Result.PropertiesWritten,
				Result.NodesCreated,
				Result.NodesMoved,
				Result.EdgesChanged,
				Result.NodesDeleted,
				Result.EntriesDeferred,
				Result.Skipped.Num());

			// Only dirty the package if something actually happened. A re-import that changes nothing should leave no trace:
			// marking it regardless makes every no-op run look like a modification, which costs a save and a diff for work
			// that was not done - and trains a designer to ignore the one signal that says an asset moved.
			const bool bChangedAnything = Result.ChangedAnything();
			if (bChangedAnything)
			{
				TargetGraph->GetPackage()->MarkPackageDirty();
			}
			else if (Result.EntriesDeferred > 0 || Result.Skipped.Num() > 0)
			{
				// Nothing was WRITTEN, but the asset does not match the source either. "Already matches" is the one line a
				// designer acts on by stopping, so it must never appear while work remains. The package still stays clean,
				// because nothing changed - that half was right.
				UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: nothing was applied, and the asset does NOT match the "
					"source - %d entry/entries could not be performed. Package left clean."),
					Result.EntriesDeferred + Result.Skipped.Num());
			}
			else
			{
				UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: nothing to apply - the asset already matches the source. "
					"Package left clean."));
			}
			return;
		}

		// P1 - create the asset via the factory, then restore the self row (with _RT identity + instanced rewards).
		// Two distinct identities: the ROW KEY (sanitized EffectiveID - folder name, tag namespace) and the authored
		// QuestlineID FIELD (raw, whatever the designer typed, spaces and all - the compiler sanitizes it only when
		// building tags, never mutating the field). The asset NAME rides the sanitized key (a package name can't hold
		// spaces); the QuestlineID FIELD must preserve the raw authored value so the round-trip doesn't alter it.
		const FQuestDataRow& SelfRow = Bundle.TablesByType[TEXT("questline_graph")].Rows[0];
		const FString OriginalKey = SelfRow.Key;                          // sanitized - folder/tag identity
		const FString RawQuestlineID = SelfRow.Get(TEXT("QuestlineID"));  // raw authored field (may be empty)
		const FString AssetName = OriginalKey + TEXT("_RT");              // _RT so the compiled tag namespace doesn't collide.

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UQuestlineGraphFactory* Factory = NewObject<UQuestlineGraphFactory>();
		UObject* Created = AssetTools.CreateAsset(AssetName, DestPackagePath, UQuestlineGraph::StaticClass(), Factory);
		UQuestlineGraph* Graph = Cast<UQuestlineGraph>(Created);
		if (!Graph || !Graph->QuestlineEdGraph)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("ImportQuestline: asset creation failed at '%s/%s'."), *DestPackagePath, *AssetName);
			return;
		}
		
		TSet<FString> Consumed;

		// Self-row properties onto the graph object (QuestlineID gets _RT; instanced QuestlineRewards rebuilt).
		RestoreQuestRowProperties(Graph, SelfRow);
		{
			// QuestlineID handling for the round-trip. Two cases, because GetEffectiveID() falls back to the ASSET
			// NAME when the field is empty:
			//   - Source field NON-empty: set the RT field to <raw>_RT, so re-export's QuestlineID cell matches the
			//     source's modulo _RT.
			//   - Source field EMPTY (asset-name-derived): LEAVE IT EMPTY. The source's EffectiveID was its asset
			//     name (e.g. "QL_Ch5_Blocking"); the RT asset's name is "<name>_RT", so the same empty->asset-name
			//     fallback yields "<name>_RT" - matching the source modulo _RT. Writing the literal "_RT" here (the
			//     prior bug) would make QuestlineID = "_RT", tags = SimpleQuest.Questline._RT.*, and the export folder
			//     "_RT" - diverging from the asset-name identity the source actually used.
			if (!RawQuestlineID.IsEmpty())
			{
				if (FProperty* IDProp = Graph->GetClass()->FindPropertyByName(TEXT("QuestlineID")))
				{
					const FString RT = RawQuestlineID + TEXT("_RT");
					IDProp->ImportText_Direct(*RT, IDProp->ContainerPtrToValuePtr<void>(Graph), nullptr, PPF_None);
				}
			}
			// else: RestoreQuestRowProperties already left it empty (the source cell was empty) - nothing to do.
		}
		ReattachQuestInstancedChildren(Graph, OriginalKey, Bundle, Consumed, Warnings);   // self-row child keys are prefixed by the self key

		// P2 - spawn nodes, root graph first, recursing into container inner graphs.
		TMap<FString, UEdGraphNode*> NodeByKey;
		ImportQuestGraphLevel(Graph->QuestlineEdGraph, TEXT("root"), Bundle, NodeRowsByKey, NodeByKey, Consumed, Warnings);

		// P3 - pin refresh pass (innermost-first).
		RefreshQuestNodePins(Bundle, NodeRowsByKey, NodeByKey, Warnings);

		// P4 - wire edges + contains-edge cross-check.
		WireQuestEdges(Bundle, NodeByKey, Consumed, Warnings);

		// Wrap the double-compile in a compile batch so the gameplay-tag-tree rebuild coalesces to ONCE (at EndCompileBatch)
		// instead of once PER compile pass. The batch's incremental AddNativeTagsForGraph keeps pass 2's RequestGameplayTag
		// lookups valid against pass 1's registrations (that's exactly what the first-compile-identity double-compile needs),
		// while the expensive tree rebuild + INI write defer to batch end. Same mechanism CompileAllQuestlineGraphs uses.
		ISimpleQuestEditorModule::Get().BeginCompileBatch();
		TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
		Compiler->Compile(Graph);                          // pass 1: registers the identity + state tags
		const bool bCompiled = Compiler->Compile(Graph);   // pass 2: identity now valid -> complete resolution records
		ISimpleQuestEditorModule::Get().EndCompileBatch();

		UPackage* Package = Graph->GetPackage();
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Graph);
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Graph, *FileName, SaveArgs);

		for (const FString& W : Warnings) UE_LOG(LogSimpleQuestResolver, Warning, TEXT("ImportQuestline: %s"), *W);
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("ImportQuestline: '%s' -> '%s/%s' - %d node(s), %d edge(s), %d warning(s), compile %s. Run C (re-export + diff) and B2 (DumpCompiled + diff) to verify."),
			*OriginalKey,
			*DestPackagePath,
			*AssetName,
			NodeByKey.Num(),
			Bundle.Edges.Num(),
			Warnings.Num(),
			bCompiled ? TEXT("OK") : TEXT("FAILED"));
	}
}

// ---- Transform seam --------------------------------------------------------------------------------------------
// External linkage for the transforms above, without moving them away from the routing code they belong with.
// See Resolver/QuestBundleTransforms.h.

bool QuestBundle_ApplyMapping(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
{
	return ApplyMapping(Bundle, Mapping, Warnings);
}

void QuestBundle_ApplyWireBindings(FQuestDataBundle& Bundle, const UQuestImportMapping& Mapping, TArray<FString>& Warnings)
{
	ApplyWireBindings(Bundle, Mapping, Warnings);
}

bool QuestBundle_Validate(const FQuestDataBundle& Bundle, TMap<FString, const FQuestDataRow*>& NodeRowsByKey, TSet<FString>& AllRowKeys, FString& OutError)
{
	return ValidateBundle(Bundle, NodeRowsByKey, AllRowKeys, OutError);
}

void QuestBundle_PlanInPlace(const UQuestlineGraph& Target, const FQuestDataBundle& Bundle, const TMap<FString, const FQuestDataRow*>& NodeRowsByKey, const TArray<FString>& ReadWarnings, FQuestInPlacePlan& OutPlan, const FQuestAbsentPolicyResolver& Policies)
{
	PlanInPlace(Target, Bundle, NodeRowsByKey, ReadWarnings, OutPlan, Policies);
}

static FAutoConsoleCommand GImportQuestlineCmd(
	TEXT("SimpleQuest.ImportQuestline"),
	TEXT("PROTOTYPE: reconstruct a questline asset from an interlingua table folder (an ExportQuestline output) and "
		"compile it. Creates a fresh <QuestlineID>_RT asset. Args: <FolderPath> <DestPackagePath> (e.g. "
		"\"E:/.../Saved/QuestExport/QL_Ch1_BasicTrigger\" /Game/Imported)."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ImportQuestlineCmd));

static FAutoConsoleCommand GEnumerateSourceColumnsCmd(
	TEXT("SimpleQuest.EnumerateSourceColumns"),
	TEXT("PROTOTYPE: list the columns a foreign source exposes (proves the source-column provider seam). "
		"Args: <SourceFolder> [--format=<name>] (default TSV)."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("EnumerateSourceColumns: usage <SourceFolder> [--format=<name>]"));
			return;
		}
		// The console tokenizes on whitespace and strips quotes, so a path with spaces arrives as MULTIPLE args. Re-join all
		// non-flag args with spaces to reconstruct the folder (quoted or not); --format=<name> is the only recognized flag.
		FString Folder;
		FString FormatName = TEXT("TSV");
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("--format=")))
			{
				FormatName = Arg.RightChop(9);
			}
			else
			{
				if (!Folder.IsEmpty()) Folder += TEXT(" ");
				Folder += Arg;
			}
		}
		Folder = Folder.TrimStartAndEnd().TrimQuotes();   // tolerate stray outer quotes / padding if any survived

		const FQuestSourceColumns Cols = EnumerateForeignFileColumns(FormatName, Folder);
		if (!Cols.bReadable)
		{
			UE_LOG(LogSimpleQuestResolver, Error, TEXT("EnumerateSourceColumns: %s"), *Cols.Error.ToString());
			return;
		}
		FString Joined;
		for (const FName& C : Cols.Columns) Joined += (Joined.IsEmpty() ? TEXT("") : TEXT(", ")) + C.ToString();
		UE_LOG(LogSimpleQuestResolver, Log, TEXT("EnumerateSourceColumns: %d column(s)%s: %s"),
			Cols.Columns.Num(), Cols.bHasDuplicateColumns ? TEXT(" [DUPLICATE]") : TEXT(""), *Joined);
	}));
