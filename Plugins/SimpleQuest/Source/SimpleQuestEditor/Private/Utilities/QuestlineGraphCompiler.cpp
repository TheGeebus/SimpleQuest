// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestlineGraphCompiler.h"

#include "GameplayTagsManager.h"
#include "ISimpleQuestEditorModule.h"
#include "SimpleQuestLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleEntry.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleExit.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupEntry.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupExit.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteFactTag.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOutcome.h"
#include "Nodes/Utility/QuestlineNode_SetBlocked.h"
#include "Nodes/Utility/QuestlineNode_ClearBlocked.h"
#include "Nodes/Utility/QuestlineNode_StartQuestline.h"
#include "Nodes/Utility/QuestlineNode_PrereqGate.h"
#include "Nodes/Utility/QuestlineNode_AddFact.h"
#include "Nodes/Utility/QuestlineNode_ClearFact.h"
#include "Nodes/Utility/QuestlineNode_RemoveFact.h"
#include "Nodes/Utility/QuestlineNode_Reward.h"
#include "Objectives/QuestObjective.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestStep.h"
#include "Quests/Quest.h"
#include "Quests/QuestRewardNode.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Quests/QuestPrereqRuleNode.h"
#include "Quests/SetBlockedNode.h"
#include "Quests/ClearBlockedNode.h"
#include "Quests/StartQuestlineNode.h"
#include "Quests/PrereqGateNode.h"
#include "Quests/ActivationGroupListenerNode.h"
#include "Quests/ActivationGroupSetterNode.h"
#include "Quests/AddFactNode.h"
#include "Quests/ClearFactNode.h"
#include "Quests/RemoveFactNode.h"
#include "Quests/Types/QuestOutcomeTags.h"
#include "Rewards/QuestRewardBase.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "Toolkit/QuestlineGraphEditor.h"
#include "Types/QuestPinRole.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Utilities/QuestTagComposer.h"
#include "Utilities/SimpleQuestEditorUtils.h"


/**
 * Walks upstream from Pin through any chain of UQuestlineNode_Knot reroute nodes. Returns true if any
 * upstream path eventually lands on a UQuestlineNode_Entry by passing only through Knot nodes; false
 * otherwise. VisitedKnots guards against pathological knot-cycle authoring (shouldn't happen but cheap).
 */
static bool DoesPinReachEntryThroughKnots(const UEdGraphPin* Pin, TSet<const UEdGraphNode*>& VisitedKnots)
{
	if (!Pin) return false;

	for (const UEdGraphPin* UpstreamPin : Pin->LinkedTo)
	{
		if (!UpstreamPin) continue;
		const UEdGraphNode* UpstreamNode = UpstreamPin->GetOwningNode();
		if (!UpstreamNode) continue;

		if (Cast<UQuestlineNode_Entry>(UpstreamNode))
		{
			return true;
		}

		if (const UQuestlineNode_Knot* KnotNode = Cast<UQuestlineNode_Knot>(UpstreamNode))
		{
			if (VisitedKnots.Contains(KnotNode)) continue;
			VisitedKnots.Add(KnotNode);

			for (const UEdGraphPin* KnotInputPin : KnotNode->Pins)
			{
				if (KnotInputPin && KnotInputPin->Direction == EGPD_Input)
				{
					if (DoesPinReachEntryThroughKnots(KnotInputPin, VisitedKnots))
					{
						return true;
					}
				}
			}
		}
		// Any non-Entry, non-Knot upstream node halts this path — the cycle would be broken by
		// whatever lifecycle guard that node enforces on the second activation pass.
	}
	return false;
}

/**
 * Duplicate a questline's authored QuestlineRewards onto OwnerGraph (the enclosing/root graph that owns all compiled
 * instances) and record them under IdentityName in OwnerGraph->CompiledQuestlineRewards, so the runtime can deliver and
 * advertise them without loading SourceGraph — which, for a linked questline, is never loaded at runtime. Mirrors the
 * per-node reward duplication the reward-node compile path uses (DuplicateObject with the owning graph as outer).
 */
void FQuestlineGraphCompiler::HarvestQuestlineRewards(const UQuestlineGraph* SourceGraph, UQuestlineGraph* OwnerGraph, FName IdentityName)
{
	if (!SourceGraph || !OwnerGraph || IdentityName.IsNone()) return;

	const TMap<FGameplayTag, FQuestRewardSet>& Authored = SourceGraph->GetQuestlineRewards();
	if (Authored.IsEmpty()) return;

	FQuestCompiledQuestlineRewards Compiled;
	for (const TPair<FGameplayTag, FQuestRewardSet>& OutcomePair : Authored)
	{
		if (!OutcomePair.Key.IsValid() || OutcomePair.Value.Rewards.IsEmpty()) continue;

		FQuestRewardSet DuplicatedSet;
		DuplicatedSet.Rewards.Reserve(OutcomePair.Value.Rewards.Num());
		const FString OutcomeSegment = OutcomePair.Key.ToString().Replace(TEXT("."), TEXT("_"));
		const FString SourceSegment = IdentityName.ToString().Replace(TEXT("."), TEXT("_"));
		for (int32 RewardIndex = 0; RewardIndex < OutcomePair.Value.Rewards.Num(); ++RewardIndex)
		{
			// Outer is the GRAPH, so the name must be unique across everything this compile puts there - and this function
			// runs once PER SOURCE QUESTLINE, every one of them outering into the same graph. Two questlines sharing an
			// outcome would otherwise claim the same name, which is a hard crash when their reward classes differ.
			// IdentityName is the per-source key these are recorded under, so it is exactly the missing segment.
			const TObjectPtr<UQuestRewardBase>& Authored_Reward = OutcomePair.Value.Rewards[RewardIndex];
			const FString RewardName = FString::Printf(TEXT("QuestlineReward_%s_%s_%d"), *SourceSegment, *OutcomeSegment, RewardIndex);
			DuplicatedSet.Rewards.Add(Authored_Reward
				? DuplicateObject<UQuestRewardBase>(Authored_Reward, OwnerGraph, *RewardName)
				: nullptr);
		}
		Compiled.RewardsByOutcome.Add(OutcomePair.Key, MoveTemp(DuplicatedSet));
	}

	if (Compiled.RewardsByOutcome.Num() > 0)
	{
		OwnerGraph->CompiledQuestlineRewards.Add(IdentityName, MoveTemp(Compiled));
	}
}

/**
 * Checksum archive that sees what a package save sees, and nothing else.
 *
 * FArchiveObjectCrc32 is not a persistent archive, so it walks Transient properties too. Those never reach the
 * .uasset, so comparing them makes the dirty guard report a change for state that was never going to be written -
 * an asset that would serialize byte-identically still gets marked dirty. CompiledEditorNodes is exactly this case:
 * rebuilt every compile, transient, and on every questline, which is why it moved the checksum universally.
 */
class FQuestCompiledCrc32 : public FArchiveObjectCrc32
{
public:
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
	{
		if (InProperty && InProperty->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) return true;
		return FArchiveObjectCrc32::ShouldSkipProperty(InProperty);
	}
};

/**
 * Re-home a display FText into the package that will actually save it, keeping its localization key.
 *
 * Compiled nodes copy their text from the authoring node, which for a linked questline lives in a DIFFERENT package.
 * UE stamps every saved FText with its owning package's localization namespace, and the default copy method mints a
 * NEW key whenever that namespace changes. So the text arrives carrying the source package's namespace, saving
 * re-keys it into this one, and the next compile drags the source namespace back in - a loop that rewrites the
 * .uasset on every compile with no authored change behind it. PreserveKey keeps existing translations attached.
 */
static FText RehomeDisplayText(const FText& Source, UObject* Target)
{
	if (Source.IsEmpty() || !Target) return Source;
	return TextNamespaceUtil::CopyTextToPackage(Source, Target,	TextNamespaceUtil::ETextCopyMethod::PreserveKey, true);
}

/**
 * Checksums the compiled model into LABELED components: the ed-graph, the graph itself, then every compiled node in
 * sorted key order.
 *
 * Deliberately NOT a hand-listed set of fields - the checksum archive walks whatever serialization walks, which is
 * whatever the .uasset contains, so a compiled field added later is covered without anyone enrolling it here.
 *
 * The ed-graph gets its own entry even though the graph-level checksum already recurses into it, because that is the
 * one component that is compiler INPUT rather than output. Separating it means a mismatch can say which side moved.
 */
static void CompiledFingerprint(UQuestlineGraph* Graph, TMap<FName, uint32>& Out)
{
	Out.Reset();
	if (!Graph) return;

	if (Graph->QuestlineEdGraph)
	{
		FQuestCompiledCrc32 EdCrc;
		Out.Add(TEXT("EdGraph"), EdCrc.Crc32(Graph->QuestlineEdGraph));
	}

	FQuestCompiledCrc32 GraphCrc;
	Out.Add(TEXT("Graph"), GraphCrc.Crc32(Graph));

	for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Compiled : Graph->GetCompiledNodes())
	{
		FQuestCompiledCrc32 NodeCrc;
		Out.Add(Compiled.Key, Compiled.Value ? NodeCrc.Crc32(Compiled.Value) : 0u);
	}

	// Diagnostic only: split the graph-level checksum into one component per compiled container, so a mismatch names
	// the field instead of just the object. Built only when the log will actually show it - the strings are not free.
	if (UE_LOG_ACTIVE(LogSimpleQuestCompiler, Verbose))
	{
		auto CrcOf = [](const TArray<FString>& Parts) { return FCrc::StrCrc32(*FString::Join(Parts, TEXT("|"))); };
		TArray<FString> Parts;

		for (const FName& Tag : Graph->GetCompiledQuestTags()) Parts.Add(Tag.ToString());
		Out.Add(TEXT("G.CompiledQuestTags"), CrcOf(Parts)); Parts.Reset();

		for (const FName& Tag : Graph->GetEntryNodeTags()) Parts.Add(Tag.ToString());
		Out.Add(TEXT("G.EntryNodeTags"), CrcOf(Parts)); Parts.Reset();

		for (const FQuestCompiledNodeAlias& Alias : Graph->GetCompiledNodeAliases())
			Parts.Add(FString::Printf(TEXT("%s>%s"), *Alias.ContextualFName.ToString(), *Alias.AliasFName.ToString()));
		Out.Add(TEXT("G.CompiledNodeAliases"), CrcOf(Parts)); Parts.Reset();

		for (const FGameplayTag& Tag : Graph->GetOutwardSetterGroupTags()) Parts.Add(Tag.ToString());
		Out.Add(TEXT("G.OutwardSetterGroupTags"), CrcOf(Parts)); Parts.Reset();

		for (const FGameplayTag& Tag : Graph->GetListenerGroupTags()) Parts.Add(Tag.ToString());
		Out.Add(TEXT("G.ListenerGroupTags"), CrcOf(Parts)); Parts.Reset();

		// Key ORDER, not just membership - a TMap that holds the same pairs in a different layout serializes to
		// different bytes, and that is one of the candidate churn sources.
		for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : Graph->GetCompiledNodes())
			Parts.Add(FString::Printf(TEXT("%s=%s"), *Pair.Key.ToString(), Pair.Value ? *Pair.Value->GetName() : TEXT("null")));
		Out.Add(TEXT("G.CompiledNodesOrder"), CrcOf(Parts)); Parts.Reset();

		for (const TPair<FName, FQuestCompiledQuestlineRewards>& Source : Graph->GetCompiledQuestlineRewards())
		{
			for (const TPair<FGameplayTag, FQuestRewardSet>& Set : Source.Value.RewardsByOutcome)
			{
				for (const TObjectPtr<UQuestRewardBase>& Reward : Set.Value.Rewards)
					Parts.Add(FString::Printf(TEXT("%s/%s/%s"), *Source.Key.ToString(), *Set.Key.ToString(),
						Reward ? *Reward->GetName() : TEXT("null")));
			}
		}
		Out.Add(TEXT("G.CompiledQuestlineRewards"), CrcOf(Parts));
	}
}

/** Renders an object's saved properties as text, one per line, so two compiles can be diffed field by field. */
static FString DumpSavedProperties(UObject* Obj)
{
	if (!Obj) return FString();
	TStringBuilder<4096> Sb;
	for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;
		FString Value;
		Prop->ExportText_InContainer(0, Value, Obj, Obj, Obj, PPF_None);
		Sb << Prop->GetName() << TEXT(" = ") << Value << TEXT("\n");
	}
	return FString(Sb);
}

/** Logs only the lines that differ between two property dumps. Verbose-only; the dumps are large. */
static void LogPropertyDelta(const FString& GraphName, FName NodeKey, const FString& Before, const FString& After)
{
	TArray<FString> B, A;
	Before.ParseIntoArrayLines(B);
	After.ParseIntoArrayLines(A);
	for (int32 i = 0; i < FMath::Max(B.Num(), A.Num()); ++i)
	{
		const FString& Bl = B.IsValidIndex(i) ? B[i] : FString();
		const FString& Al = A.IsValidIndex(i) ? A[i] : FString();
		if (Bl != Al)
		{
			UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("  Delta '%s'/'%s':\n    was: %s\n    now: %s"),
				*GraphName, *NodeKey.ToString(), *Bl, *Al);
		}
	}
}

/** Logs every component that moved between two fingerprints, and returns whether any did. */
static bool FingerprintDiffers(const TMap<FName, uint32>& Before, const TMap<FName, uint32>& After, const FString& GraphName)
{
	bool bDiffers = false;
	for (const TPair<FName, uint32>& Entry : After)
	{
		const uint32* Prior = Before.Find(Entry.Key);
		if (!Prior)
		{
			UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("Fingerprint '%s': ADDED '%s'"), *GraphName, *Entry.Key.ToString());
			bDiffers = true;
		}
		else if (*Prior != Entry.Value)
		{
			UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("Fingerprint '%s': CHANGED '%s' (0x%08X -> 0x%08X)"),
				*GraphName, *Entry.Key.ToString(), *Prior, Entry.Value);
			bDiffers = true;
		}
	}
	for (const TPair<FName, uint32>& Entry : Before)
	{
		if (!After.Contains(Entry.Key))
		{
			UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("Fingerprint '%s': REMOVED '%s'"), *GraphName, *Entry.Key.ToString());
			bDiffers = true;
		}
	}
	return bDiffers;
}

uint32 FQuestlineGraphCompiler::ComputeSourceHash(UQuestlineGraph* Root)
{
	if (!Root) return 0;

	// FORWARD-only walk, deliberately: this answers "what does my compiled output depend on," which is this graph plus
	// everything it inlines. The module's CollectLinkedNeighborhood is bidirectional - correct for deciding what to
	// RECOMPILE, wrong here, because a parent editing its own graph would mark this child stale when nothing about the
	// child changed.
	TSet<UQuestlineGraph*> Seen;
	TArray<UQuestlineGraph*> Frontier;
	TMap<FString, uint32> CrcByPath;
	Frontier.Add(Root);

	while (Frontier.Num() > 0)
	{
		UQuestlineGraph* Current = Frontier.Pop(EAllowShrinking::No);
		if (!Current || Seen.Contains(Current) || !Current->QuestlineEdGraph) continue;
		Seen.Add(Current);

		FQuestCompiledCrc32 Crc;
		const uint32 GraphCrc = Crc.Crc32(Current->QuestlineEdGraph);
		CrcByPath.Add(Current->GetPathName(), GraphCrc);

		UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("SourceHash WALK '%s' = 0x%08X"), *Current->GetName(), GraphCrc);

		for (UEdGraphNode* Node : Current->QuestlineEdGraph->Nodes)
		{
			if (UQuestlineNode_LinkedQuestline* Linked = Cast<UQuestlineNode_LinkedQuestline>(Node))
			{
				if (UQuestlineGraph* Inner = Linked->LinkedGraph.LoadSynchronous()) Frontier.Add(Inner);
			}
		}
	}

	// Sorted by path so the result does not depend on traversal order - the compiler and the editor's status check reach
	// the same set by different routes, and an order-sensitive combine would make them disagree permanently.
	TArray<FString> Paths;
	CrcByPath.GenerateKeyArray(Paths);
	Paths.Sort();

	uint32 Combined = 0;
	for (const FString& Path : Paths) Combined = HashCombine(Combined, CrcByPath[Path]);
	return Combined;
}

FQuestlineGraphCompiler::FQuestlineGraphCompiler()
    : TraversalPolicy(MakeUnique<FQuestlineGraphTraversalPolicy>())
{
}

FQuestlineGraphCompiler::~FQuestlineGraphCompiler() = default;

// -------------------------------------------------------------------------------------------------
// Entry point
// -------------------------------------------------------------------------------------------------

bool FQuestlineGraphCompiler::Compile(UQuestlineGraph* InGraph)
{
    if (!InGraph || !InGraph->QuestlineEdGraph)
    {
        AddError(TEXT("Invalid graph asset. QuestlineEdGraph is null."));
        return false;
    }

	TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_Compile);

    bHasErrors = false;
    Messages.Empty();
    NumErrors = 0;
    NumWarnings = 0;
    RootGraph = InGraph;

	// Derive the effective questline ID; designer override takes priority, asset name is the fallback
	const FString TagPrefix = SanitizeTagSegment(InGraph->QuestlineID.IsEmpty() ? InGraph->GetName() : InGraph->QuestlineID);

	// A QuestlineID of only whitespace is NOT IsEmpty(), so the asset-name fallback above never fires — and the sanitizer
	// trims it away to nothing. An empty prefix composes the tag "SimpleQuest.Questline." which the engine rejects for its
	// trailing period and SILENTLY substitutes with the bare root "SimpleQuest.Questline" — so every node would compile onto
	// a tag that is not this questline's, while the compile still reported success. Refuse it, exactly as an empty node
	// label is refused.
	if (TagPrefix.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("QuestlineID '%s' contains no usable characters - it reduces to an empty tag segment. Give it at least one "
				 "letter, digit or underscore, or clear the field entirely to fall back to the asset name."),
			*InGraph->QuestlineID));
		return false;
	}

    // Validate that no other questline asset shares this effective ID
    IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
    TArray<FAssetData> AllQuestlineGraphs;
    FARFilter Filter;
    Filter.ClassPaths.Add(UQuestlineGraph::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    AssetRegistry.GetAssets(Filter, AllQuestlineGraphs);

    for (const FAssetData& Asset : AllQuestlineGraphs)
    {
        if (Asset.GetObjectPathString() == InGraph->GetPathName()) continue;
        FAssetTagValueRef TagValue = Asset.TagsAndValues.FindTag(TEXT("QuestlineEffectiveID"));
        if (TagValue.IsSet() && SanitizeTagSegment(TagValue.GetValue()) == TagPrefix)
        {
            AddError(FString::Printf(
                TEXT("QuestlineID '%s' is already used by '%s'. Set a unique QuestlineID on one of these assets to resolve the conflict."),
                *TagPrefix,
                *Asset.GetObjectPathString()));
            return false;
        }
    }

	// ── Validate questline-level rewards against the graph's top-level Exit outcomes ──
	// QuestlineRewards is keyed by outcome tag; each key MUST correspond to a top-level Exit's OutcomeTag on this graph.
	// A stale key (its Exit was retyped/removed) would silently grant nothing at runtime — refuse it at compile so the
	// designer fixes the key (re-picking preserves the reward values). Gather this graph's root-scope Exit outcomes once,
	// then check every authored key against them.
	if (InGraph->QuestlineRewards.Num() > 0)
	{
		TSet<FGameplayTag> TopLevelExitOutcomes;
		for (UEdGraphNode* Node : InGraph->QuestlineEdGraph->Nodes)
		{
			if (const UQuestlineNode_Exit* ExitNode = Cast<UQuestlineNode_Exit>(Node))
			{
				if (ExitNode->OutcomeTag.IsValid()) TopLevelExitOutcomes.Add(ExitNode->OutcomeTag);
			}
		}
		// Any-Outcome is a valid key that is NEVER an Exit outcome (it means "on every completion, regardless of
		// outcome"). Treat it as always-current so it isn't flagged as drift. Matches the details panel's picker + stale check.
		TopLevelExitOutcomes.Add(TAG_Outcome_AnyOutcome.GetTag());

		for (const TPair<FGameplayTag, FQuestRewardSet>& Pair : InGraph->QuestlineRewards)
		{
			if (Pair.Value.Rewards.IsEmpty()) continue;
			if (!TopLevelExitOutcomes.Contains(Pair.Key))
			{
				AddError(FString::Printf(
					TEXT("[%s] Questline-level rewards are keyed on outcome '%s', but no top-level Exit node on this questline "
						 "resolves with that outcome. Re-key the reward entry to a current Exit outcome (its rewards carry over), "
						 "or remove it."),
					*TagPrefix, *Pair.Key.ToString()));
				return false;
			}
		}
	}
    
    UE_LOG(LogSimpleQuestCompiler, Log, TEXT("Compile: starting '%s' (prefix='%s')"),
        *InGraph->GetName(),
        *TagPrefix);

    // ── Snapshot old GUID→Tag mapping for rename detection ────────
    TMap<FGuid, FName> OldTagsByGuid;
    for (const auto& [TagName, NodeInstance] : InGraph->CompiledNodes)
    {
        if (NodeInstance && NodeInstance->GetQuestGuid().IsValid())
        {
            OldTagsByGuid.Add(NodeInstance->GetQuestGuid(), TagName);
        }
    }
	CurrentOuterGuidChain = FGuid();
    DetectedTagRenames.Empty();

	// Clear parallel-path tracking so a fresh compile doesn't inherit stale data from a prior compile.
	DirectReachesByDest.Empty();
	GroupSetterSourcesByTag.Empty();
	GroupGetterDestsByTag.Empty();
	SetterEdNodeByGroupAndSource.Empty();
	GetterEdNodeByGroupAndDest.Empty();
	ImmediateContainerByTag.Empty();
	CurrentInnerContainerTag = NAME_None;
	CompiledSetterGroupTags.Reset();
	CompiledListenerGroupTags.Reset();
	
	// Fingerprint what is already compiled BEFORE tearing it down, so the end of the compile can tell whether anything
	// actually changed. Modify() is deliberately NOT called here: it dirties the package before the compile can possibly
	// know whether it will change anything, which is how eleven untouched assets end up wanting to be saved.
	TMap<FName, uint32> PreCompileFingerprint;
	CompiledFingerprint(InGraph, PreCompileFingerprint);
	const bool bPackageWasDirtyBeforeCompile = InGraph->GetPackage() && InGraph->GetPackage()->IsDirty();

	// Verbose-only: capture each compiled node's saved properties as text NOW, while the previous compile's nodes are
	// still alive. After the teardown below they are gone, and a checksum delta on its own cannot name a field.
	TMap<FName, FString> PreCompileNodeText;
	if (UE_LOG_ACTIVE(LogSimpleQuestCompiler, Verbose))
	{
		for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : InGraph->GetCompiledNodes())
		{
			PreCompileNodeText.Add(Pair.Key, DumpSavedProperties(Pair.Value));
		}
	}

	// Move the previous compile's subobjects out of the graph before dropping them. They survive until the next GC, and
	// the new pass names its nodes deterministically from their tags - so without this, every recompile would collide
	// with its own predecessor and UE would silently uniquify the name, reintroducing the churn this exists to remove.
	for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Stale : InGraph->CompiledNodes)
	{
		if (UQuestNodeBase* Node = Stale.Value)
		{
			Node->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
		}
	}

	// Questline-level rewards are outered to the graph itself, so they outlive the map that references them exactly as the
	// nodes do, and their now-stable names would collide with the next compile's.
	for (const TPair<FName, FQuestCompiledQuestlineRewards>& StaleGraph : InGraph->CompiledQuestlineRewards)
	{
		for (const TPair<FGameplayTag, FQuestRewardSet>& StaleSet : StaleGraph.Value.RewardsByOutcome)
		{
			for (const TObjectPtr<UQuestRewardBase>& StaleReward : StaleSet.Value.Rewards)
			{
				if (StaleReward)
				{
					StaleReward->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
				}
			}
		}
	}

	InGraph->CompiledNodes.Empty(); 
	InGraph->CompiledQuestlineRewards.Empty();
	InGraph->EntryNodeTags.Empty();
	InGraph->CompiledQuestTags.Empty();
	InGraph->CompiledNodeAliases.Empty();
	AllCompiledNodes.Empty();
	UtilityNodeKeyMap.Empty();
	CompiledAliasFNamesByContextualTag.Empty();
	RootGraph = InGraph;

	// Compose the root asset's identity tag (SimpleQuest.Questline.<AssetSegment>) so any Exit visited at
	// the root asset's root scope can attribute its graph-resolution publish to this asset. Save/restore
	// around LinkedQuestline recursion (below) so inner asset Exits attribute to the linked asset instead.
	// Add to AllCompiledQuestTags so RegisterCompiledQuestTags expands the asset-identity state-leaf facts
	// (SimpleQuest.State.<AssetSegment>.{Live, Completed, ...}) at module init — required for the runtime
	// PublishGraphResolutions WSV write to land at a registered fact tag rather than no-op on an unregistered
	// one.
	const FName RootAssetIdentityName = FQuestTagComposer::MakeIdentityTag(TagPrefix, {});
	AllCompiledQuestTags.Add(RootAssetIdentityName);
	CurrentAssetIdentityTag = UGameplayTagsManager::Get().RequestGameplayTag(RootAssetIdentityName, false);

    // Refresh outcome pins on all step nodes so that changes to outcomes on an objective class are reflected without
    // the designer having to touch ObjectiveClass again.
    for (UEdGraphNode* Node : InGraph->QuestlineEdGraph->Nodes)
    {
        if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(Node)) StepNode->RefreshOutcomePins();
    }

    // The graphs that have already been compiled. Provided to CompileGraph, which forwards it to all recursive calls.
    TArray<FString> VisitedAssetPaths;
    VisitedAssetPaths.Add(InGraph->GetPathName());

	TMap<FName, TArray<FQuestBoundaryCompletion>> BoundaryCompletionsByPath;

    // Start recursive compilation, working forward from the Start node.
    TArray<FName> EntryTags = CompileGraph(
    	InGraph->QuestlineEdGraph,
    	TagPrefix,
    	{},
    	BoundaryCompletionsByPath,
    	VisitedAssetPaths,
    	nullptr,
    	ResolveResettable(InGraph->GetResettableReplay(), false));
	
    InGraph->EntryNodeTags = EntryTags;
    InGraph->CompiledNodes = MoveTemp(AllCompiledNodes);
    InGraph->CompiledEditorNodes = MoveTemp(AllCompiledEditorNodes);
    InGraph->CompiledQuestTags = MoveTemp(AllCompiledQuestTags);
	
	// This graph's own questline-level rewards, harvested under its root identity (linked children are harvested
	// separately inside the LinkedQuestline recursion below, each under its own inner identity).
	HarvestQuestlineRewards(InGraph, InGraph, RootAssetIdentityName);

	// Flatten contextual→alias map into the persisted pairs array. One entry per (Contextual, Alias) pair so a node with
	// N aliases produces N entries; nodes without aliases (top-level, no LinkedQuestline ancestors) produce none.
	for (const TPair<FName, TArray<FName>>& Entry : CompiledAliasFNamesByContextualTag)
	{
		for (const FName& AliasFName : Entry.Value)
		{
			FQuestCompiledNodeAlias Pair;
			Pair.ContextualFName = Entry.Key;
			Pair.AliasFName = AliasFName;
			InGraph->CompiledNodeAliases.Add(Pair);
		}
	}
	InGraph->OutwardSetterGroupTags = CompiledSetterGroupTags.Array();
	InGraph->ListenerGroupTags = CompiledListenerGroupTags.Array();
	
    // Detect renames via GUID bridge
    DetectAndRecordTagRenames(InGraph, OldTagsByGuid);

	// Name each compiled subobject from its own registry key rather than letting NewObject auto-number it. UE's default
	// naming draws from a per-class counter that keeps advancing, so an unchanged graph produced QuestStep_12 on one
	// compile and QuestStep_47 on the next - different export tables, a rewritten .uasset, and a binary diff on a graph
	// nobody touched. The key is unique per compiled node by construction, so it is both stable and collision-free.
	for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Compiled : InGraph->CompiledNodes)
	{
		if (UQuestNodeBase* Node = Compiled.Value)
		{
			const FString StableName = Compiled.Key.ToString().Replace(TEXT("."), TEXT("_"));
			if (Node->GetFName() != FName(*StableName))
			{
				Node->Rename(*StableName, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty);
			}
		}
	}

	RegisterCompiledTags(InGraph);
	// Phase 1 of container lifecycle alignment - compute structural reachability data so the downstream
	// lifecycle methods (SetQuestLive auto-propagation, path-aware giver gate, etc.) can branch on
	// structural containment rather than re-deriving it at runtime.
	ComputeContainerReachability(InGraph);
	BuildRewardManifest(InGraph);

	// Stamp the authoring input's checksum BEFORE the comparison below, so it sits inside the compared window: a source
	// edit moves the hash, the comparison sees it, and the asset dirties. Outside the window it would silently diverge
	// from what was saved and the status icon would report stale forever.
	InGraph->CompiledSourceHash = ComputeSourceHash(InGraph);
	
	// Dirty the package only if the compile actually produced something different. Two ordering constraints, both of them
	// load-bearing. It must come AFTER the naming pass, because only then do old and new nodes share names and the object
	// references inside them resolve to identical paths - before it, every reference differs and this could only ever
	// report "changed". And it must come after the LAST writer of compiled state, which is BuildRewardManifest, or those
	// writes land outside the compared window and the comparison silently answers a narrower question than it appears to.
	TMap<FName, uint32> PostCompileFingerprint;
	CompiledFingerprint(InGraph, PostCompileFingerprint);

	if (FingerprintDiffers(PreCompileFingerprint, PostCompileFingerprint, InGraph->GetName()))
	{
		InGraph->Modify();

		// Verbose-only: a checksum says a node moved but not which field. Pair the captured text against the fresh
		// object for every node whose checksum actually changed, so the log names the property.
		for (const TPair<FName, FString>& Prior : PreCompileNodeText)
		{
			const uint32* Was = PreCompileFingerprint.Find(Prior.Key);
			const uint32* Now = PostCompileFingerprint.Find(Prior.Key);
			if (Was && Now && *Was != *Now)
			{
				LogPropertyDelta(InGraph->GetName(), Prior.Key, Prior.Value,
					DumpSavedProperties(InGraph->GetCompiledNodes().FindRef(Prior.Key)));
			}
		}
	}
	else if (UPackage* Package = InGraph->GetPackage())
	{
		// Nothing observable changed. The subobjects are fresh instances but byte-identical in content, so a save would
		// write the same file — marking the asset dirty would only prompt the user to re-save what they already have.
		Package->SetDirtyFlag(bPackageWasDirtyBeforeCompile);
	}
	
    UE_LOG(LogSimpleQuestCompiler, Log, TEXT("Compile: '%s' finished — %d node(s), %d tag(s), %d error(s), %d warning(s)"),
        *InGraph->GetName(),
        InGraph->CompiledNodes.Num(),
        InGraph->CompiledQuestTags.Num(),
        NumErrors,
        NumWarnings);

	EmitParallelPathWarnings();
	
    return !bHasErrors;
}


// -------------------------------------------------------------------------------------------------
// CompileGraph — recursive
// -------------------------------------------------------------------------------------------------

TArray<FName> FQuestlineGraphCompiler::CompileGraph(
	UEdGraph* Graph,
	const FString& TagPrefix,
	const TArray<FString>& AssetScopedAliasPrefixes,
	const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
	TArray<FString>& VisitedAssetPaths, TMap<FName, FQuestEntryRouteList>* OutEntryTagsByPath, bool bIncomingResettable)		
{
    if (!Graph) return {};

	TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_CompileGraph);
	
    // ---- Pass 1: label uniqueness, GUID write, tag assignment ----
    TArray<UQuestlineNode_ContentBase*> ContentNodes;
    TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*> NodeInstanceMap;
    CompileNodeRegistration(Graph, TagPrefix, AssetScopedAliasPrefixes, BoundaryCompletionsByPath, VisitedAssetPaths, ContentNodes, NodeInstanceMap, bIncomingResettable);

    // ---- Pass 1b: setter nodes — create UQuestPrereqRuleNode monitors ----
    CompileGroupSetters(Graph, TagPrefix, VisitedAssetPaths);

    // ---- Pass 1c: utility nodes ----
    TArray<UQuestlineNode_UtilityBase*> UtilityEdNodes;
    CompileUtilityNodes(Graph, TagPrefix, VisitedAssetPaths, UtilityEdNodes);

    UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("CompileGraph: [%s] %d content, %d utility node(s)"),
        *TagPrefix,
        ContentNodes.Num(),
        UtilityEdNodes.Num());
    
    if (bHasErrors) return {};

    // ---- Stale pin diagnostic ----
    for (UQuestlineNode_ContentBase* ContentNode : ContentNodes)
    {
        for (UEdGraphPin* Pin : ContentNode->Pins)
        {
            if (Pin->bOrphanedPin && Pin->LinkedTo.Num() > 0)
            {
                const FString Label = ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
                AddWarning(FString::Printf(
                    TEXT("[%s] Node '%s' has a stale pin '%s' with %d active connection(s). These wires will be ignored at runtime. Right-click the node to remove stale pins."),
                    *TagPrefix, *Label, *Pin->PinName.ToString(), Pin->LinkedTo.Num()), ContentNode);
            }
        }
    }

    // ---- Pass 2: output pin wiring ----
    CompileOutputWiring(ContentNodes, NodeInstanceMap, TagPrefix, BoundaryCompletionsByPath, VisitedAssetPaths);

	// ---- Collect activation group metadata for parallel-path analysis ----
	CollectActivationGroupMetadata(Graph, TagPrefix);

	// ---- Pass 2b: Forward output wiring for utility-keyed nodes that live in THIS graph ----
	// UtilityNodeKeyMap is a compiler-wide map accumulated across recursion; iterating it unconditionally would
	// rewrite nested utility nodes with the outer graph's TagPrefix each time recursion unwinds. Scope the loop
	// to this graph's nodes so each utility node's forward wiring is resolved against the prefix it was born with.
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		const FName* UtilKey = UtilityNodeKeyMap.Find(Node);
		if (!UtilKey) continue;

		UQuestNodeBase* Inst = AllCompiledNodes.FindRef(*UtilKey);
		if (!Inst) continue;

		Inst->NextNodesOnForward.Empty();
		Inst->BoundaryCompletionsOnForward.Empty();
		Inst->ResolvedGraphsOnForward.Empty();

		if (UEdGraphPin* ForwardPin = UQuestlineNodeBase::FindPinByRole(Node, EQuestPinRole::ExecForwardOut))
		{
			// Utility forward output may cross one or more wrapper Exits: capture both the next-tag list AND the
			// boundary-completion records the walk accumulates so HandleOnNodeForwardActivated can fire wrapper
			// completion (SetQuestResolved + FQuestEndedEvent) before the chain fans out. Without this, a utility
			// node placed before a wrapper Exit silently drops the wrapper's completion: host stays Live forever,
			// Path facts never emit, downstream Path-prereqs never satisfy.
			TArray<FName> ForwardTags;
			TArray<FQuestBoundaryCompletion> ForwardBoundaries;
			TArray<FQuestGraphResolution> ForwardResolutions;
			ResolvePinToTags(ForwardPin, TagPrefix, BoundaryCompletionsByPath, VisitedAssetPaths, ForwardTags, ForwardBoundaries, nullptr, &ForwardResolutions);
			for (const FName& Tag : ForwardTags) Inst->NextNodesOnForward.Add(Tag);
			for (const FQuestBoundaryCompletion& BC : ForwardBoundaries) Inst->BoundaryCompletionsOnForward.AddUnique(BC);
			for (const FQuestGraphResolution& Res : ForwardResolutions) Inst->ResolvedGraphsOnForward.AddUnique(Res);
		}
	}

    // ---- Resolve entry tags from the graph's Entry node ----
    TArray<FName> EntryTags = ResolveEntryTags(Graph, TagPrefix, BoundaryCompletionsByPath, VisitedAssetPaths, OutEntryTagsByPath);
    return EntryTags;
}

void FQuestlineGraphCompiler::CompileNodeRegistration(
	UEdGraph* Graph,
	const FString& TagPrefix,
	const TArray<FString>& AssetScopedAliasPrefixes,
	const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
	TArray<FString>& VisitedAssetPaths,
	TArray<UQuestlineNode_ContentBase*>& OutContentNodes,
	TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*>& OutNodeInstanceMap,
	bool bIncomingResettable)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_CompileNodeRegistration);
	
    TMap<FString, UQuestlineNode_ContentBase*> LabelMap;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node);
        if (!ContentNode) continue;
        OutContentNodes.Add(ContentNode);
    	
    	/**
		 * LinkedQuestline's GetNodeTitle is driven by the referenced asset's name (so multiple placements of the same asset share
		 * a title); use NodeLabel directly to guarantee per-placement uniqueness. Other content nodes' GetNodeTitle already reflects
		 * NodeLabel via the base ContentBase path, so behavior is unchanged for them.
		 */
    	const FString Label = Cast<UQuestlineNode_LinkedQuestline>(ContentNode)
			? SanitizeTagSegment(ContentNode->NodeLabel.ToString())
			: SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    	
        if (Label.IsEmpty())
        {
            AddError(FString::Printf(TEXT("[%s] A content node has an empty label. All Quest and Step nodes must have a label before compiling."), *TagPrefix), ContentNode);
            continue;
        }
    	if (LabelMap.Contains(Label))
    	{
    		AddError(FString::Printf(TEXT("[%s] Duplicate node label '%s'. Labels must be unique within a graph."), *TagPrefix, *Label), ContentNode);
    		continue;
    	}

    	// Defensive overkill: every token FQuestTagComposer manages is reserved against label collision.
    	// AllNamespaces is the union of AllPrefixes / AllFullNamespaces / AllSuffixes / AllStateLeaves -- adding
    	// a new namespace, suffix, or leaf-kind to FQuestTagComposer extends ReservedSegments automatically with
    	// no edit here. Some entries (the fully-composed namespaces with embedded dots) can never match a
    	// single-segment label so they're effectively no-ops, but keeping them in the set is harmless and removes
    	// any "did I forget to update this?" risk class.
    	static const TSet<FString> ReservedSegments(FQuestTagComposer::AllNamespaces);
    	
    	if (ReservedSegments.Contains(Label))
    	{
    		AddWarning(FString::Printf(TEXT("[%s] Node label '%s' matches a reserved tag segment — compiles to an ambiguous tag (e.g. SimpleQuest.Questline.<...>.%s). "
				"Recommend renaming. (Warning only — compile proceeds.)"), *TagPrefix, *Label, *Label), ContentNode);
    	}
    	LabelMap.Add(Label, ContentNode);

    	// TagName computed up-front so wrapper branches can pass it as their inner CompileGraph's container tag
    	// (see CurrentInnerContainerTag save/restore below). Originally computed after the type-cascade; moved up
    	// so containment tracking can identify the just-being-compiled wrapper before recursing into its inner graph.
    	const FName TagName = MakeNodeTagName(TagPrefix, Label);

    	// Compute asset-scoped alias FNames for cross-asset routing. One per enclosing LinkedQuestline ancestor
    	// (excluding the top-level compile asset whose perspective IS TagName). Empty for top-level content. Each
    	// alias gets registered in AllCompiledQuestTags below alongside TagName so RequestGameplayTag can resolve
    	// them at the post-pass; the per-instance resolution happens in ComputeContainerReachability.
    	TArray<FName> AliasFNames;
    	AliasFNames.Reserve(AssetScopedAliasPrefixes.Num());
    	for (const FString& AliasPrefix : AssetScopedAliasPrefixes)
    	{
    		AliasFNames.Add(MakeNodeTagName(AliasPrefix, Label));
    	}
    	
    	// Effective per-run resettability: own flag wins, else inherit from above (asset / container / host).
    	const bool bNodeResettable = ResolveResettable(ContentNode->ResettableReplay, bIncomingResettable);
    	
        // Create the appropriate runtime instance
        UQuestNodeBase* Instance = nullptr;

    	if (UQuestlineNode_Quest* QuestEdNode = Cast<UQuestlineNode_Quest>(ContentNode))
    	{
    		UQuest* QuestInstance = NewObject<UQuest>(RootGraph);
    		if (QuestEdNode->GetInnerGraph())
    		{
    			TMap<FName, TArray<FQuestBoundaryCompletion>> InlineBoundaryCompletionsByPath;
    			ComputeInnerBoundaryMaps(QuestEdNode, TagPrefix, Label, BoundaryCompletionsByPath, VisitedAssetPaths, InlineBoundaryCompletionsByPath);

    			const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
    			TMap<FName, FQuestEntryRouteList> InnerEntryByPath;

    			// Inline UQuest recursion stays within the current asset — no new asset-scope perspective is added.
    			// Each existing alias prefix gets ".Label" appended (the inline UQuest deepens the path inside every
    			// asset's perspective on this content). Stack length unchanged.
    			TArray<FString> InnerAliasPrefixes;
    			InnerAliasPrefixes.Reserve(AssetScopedAliasPrefixes.Num());
    			for (const FString& AliasPrefix : AssetScopedAliasPrefixes)
    			{
    				InnerAliasPrefixes.Add(AliasPrefix + TEXT(".") + Label);
    			}
    			
    			// Save/restore CurrentInnerContainerTag around the inner CompileGraph recursion so nested
    			// registrations record this UQuest as their immediate container. Mirrors the CurrentOuterGuidChain
    			// save/restore pattern in the LinkedQuestline branch below.
    			const FName PreviousContainer = CurrentInnerContainerTag;
    			CurrentInnerContainerTag = TagName;
    			QuestInstance->EntryStepTags = CompileGraph(QuestEdNode->GetInnerGraph(), InnerPrefix, InnerAliasPrefixes, InlineBoundaryCompletionsByPath, VisitedAssetPaths, &InnerEntryByPath, bNodeResettable);
    			CurrentInnerContainerTag = PreviousContainer;

    			QuestInstance->EntryStepTagsByPath = MoveTemp(InnerEntryByPath);
    		}
    		Instance = QuestInstance;
    	}
        else if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(ContentNode))
        {
            if (StepNode->ObjectiveClass.IsNull())
            {
                AddError(FString::Printf(TEXT("[%s] Step node '%s' has no Objective Class assigned."), *TagPrefix, *Label), ContentNode);
                continue;
            }
            UQuestStep* StepInstance = NewObject<UQuestStep>(RootGraph);
            StepInstance->QuestObjective = StepNode->ObjectiveClass;
            StepInstance->TargetClasses = StepNode->TargetClasses;
            StepInstance->NumberOfElements = StepNode->NumberOfElements;
            StepInstance->TargetActors.Append(StepNode->TargetActors);
            StepInstance->PrerequisiteGateMode = StepNode->PrerequisiteGateMode;
            Instance = StepInstance;
        }
		else if (UQuestlineNode_LinkedQuestline* LinkedNode = Cast<UQuestlineNode_LinkedQuestline>(ContentNode))
		{
			if (LinkedNode->LinkedGraph.IsNull())
			{
				// Null LinkedGraph is valid — emit a UQuest instance with the node's own compiled tag so designers can
				// attach givers and reference the LinkedQuestline by tag before picking an asset. No inner routing
				// populates (empty EntryStepTags / NextNodesByPath); the instance behaves as an empty container
				// until an asset is picked and the graph recompiled. Warning is still issued so designers know the
				// compile is effectively incomplete.
				AddWarning(FString::Printf(TEXT("[%s] LinkedQuestline node '%s' has no asset assigned — runtime instance emitted with no inner routing; pick an asset to populate."),
					*TagPrefix,
					*Label),
					LinkedNode);
				{
					UQuest* LinkedInstance = NewObject<UQuest>(RootGraph);
					LinkedInstance->bIsLinkedQuestlinePlacement = true;
					Instance = LinkedInstance;
				}
			}
			else
			{
				UQuestlineGraph* LinkedGraph = LinkedNode->LinkedGraph.LoadSynchronous();
				if (!LinkedGraph || !LinkedGraph->QuestlineEdGraph)
				{
					AddError(FString::Printf(TEXT("[%s] LinkedQuestline '%s' failed to load asset '%s'."),
						*TagPrefix,
						*Label,
						*LinkedNode->LinkedGraph.ToString()),
						LinkedNode);
					continue;
				}

				// Refresh outcome pins before reading them — the linked graph's Exit tags may have changed since this node
				// was last edited or loaded, without triggering PostLoad/PostEditChangeProperty on the parent. Runs once
				// per compile, per placement, which is cheap.
				LinkedNode->RebuildOutcomePinsFromLinkedGraph();

				const FString LinkedPath = LinkedGraph->GetPathName();
				if (VisitedAssetPaths.Contains(LinkedPath))
				{
					/**
					 * Reconstruct the cycle path for the error: slice VisitedAssetPaths from the cycling asset's prior entry to
					 * the end, then close with the cycling asset name again. The cycle is a property of the chain as a whole —
					 * this link is not uniquely at fault, it just happens to be the one that closes the loop during recursion.
					 */
					const int32 CycleStart = VisitedAssetPaths.IndexOfByKey(LinkedPath);
					FString CyclePath;
					for (int32 i = CycleStart; i < VisitedAssetPaths.Num(); ++i)
					{
						CyclePath += FPackageName::ObjectPathToObjectName(VisitedAssetPaths[i]);
						CyclePath += TEXT(" → ");
					}
					CyclePath += FPackageName::ObjectPathToObjectName(LinkedPath);

					AddError(FString::Printf(
						TEXT("LinkedQuestline cycle detected: compile chain [%s]. This link closes the cycle; it is valid in isolation, "
						"but any link in this chain must be removed for compilation to succeed. Use activation group setter/getter pairs for runtime "
						"loops across assets."),
						*CyclePath),
						LinkedNode);
					continue;
				}

				UQuest* QuestInstance = NewObject<UQuest>(RootGraph);
				QuestInstance->bIsLinkedQuestlinePlacement = true;

				/**
				 * Per-path boundary tag map for the linked graph. Keys are completion path identities as FNames (matching
				 * the upstream pin name — outcome tag's full FName for static placements; sanitized PathName for dynamic
				 * placements once Bundle Y lands). "Any Outcome" pin stored under NAME_None as a catch-all.
				 */
				TMap<FName, TArray<FQuestBoundaryCompletion>> LinkedBoundaryCompletionsByPath;
				ComputeInnerBoundaryMaps(LinkedNode, TagPrefix, Label, BoundaryCompletionsByPath, VisitedAssetPaths, LinkedBoundaryCompletionsByPath);

				/**
				 * Compile the linked asset's graph as the UQuest's inner graph. TagPrefix for the inner compile is the
				 * LinkedQuestline's own compiled path — same pattern as inline Quest. Linked content nodes' compiled tags
				 * thus nest under this LinkedQuestline's tag (Quest.<ParentID>.<NodeLabel>.<InnerNodeLabel>), keeping a
				 * stable per-parent namespace when the same linked asset is referenced from multiple places.
				 */
				VisitedAssetPaths.Add(LinkedPath);

				/**
				 * Push the linked placement's GUID onto the chain so inner content nodes produce placement-unique compound
				 * GUIDs. Save/restore with local so nested LinkedQuestlines accumulate correctly through multiple levels.
				 */
				const FGuid PreviousGuidChain = CurrentOuterGuidChain;
				CurrentOuterGuidChain = CombineGuids(CurrentOuterGuidChain, LinkedNode->QuestGuid);

				const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
				TMap<FName, FQuestEntryRouteList> InnerEntryByPath;

				// LinkedQuestline recursion crosses an asset boundary — we add a new asset-scope perspective
				// (the linked asset's own QuestlineID prefix) AND every existing alias gets ".Label" appended
				// because from each previous asset's perspective, the LinkedQuestline placement is a node in
				// their content. Stack length grows by one.
				TArray<FString> InnerAliasPrefixes;
				InnerAliasPrefixes.Reserve(AssetScopedAliasPrefixes.Num() + 1);
				for (const FString& AliasPrefix : AssetScopedAliasPrefixes)
				{
					InnerAliasPrefixes.Add(AliasPrefix + TEXT(".") + Label);
				}
				const FString LinkedAssetPrefix = SanitizeTagSegment(LinkedGraph->QuestlineID.IsEmpty() ? LinkedGraph->GetName() : LinkedGraph->QuestlineID);
				InnerAliasPrefixes.Add(LinkedAssetPrefix);
				
				// Save/restore CurrentInnerContainerTag around the linked inner CompileGraph so nested
				// registrations record this LinkedQuestline placement as their immediate container.
				const FName PreviousContainer = CurrentInnerContainerTag;
				CurrentInnerContainerTag = TagName;

				// Save/restore CurrentAssetIdentityTag around the linked inner CompileGraph so any Exits
				// inside the linked asset attribute their graph-resolution publish to the linked asset's
				// identity tag (not the outer asset's). Symmetric save/restore with CurrentInnerContainerTag.
				// Linked asset identity also added to AllCompiledQuestTags so its state-leaf facts get
				// registered at module init even when the inner asset isn't auto-loaded standalone — outer
				// asset's compile is the sole source of registration in that scenario.
				const FGameplayTag PreviousAssetIdentity = CurrentAssetIdentityTag;
				const FName LinkedAssetIdentityName = FQuestTagComposer::MakeIdentityTag(LinkedAssetPrefix, {});
				AllCompiledQuestTags.AddUnique(LinkedAssetIdentityName);
				CurrentAssetIdentityTag = UGameplayTagsManager::Get().RequestGameplayTag(LinkedAssetIdentityName, false);

				// Bridge the placement to its inner asset identity: the same tag HarvestQuestlineRewards files the inner
				// questline's rewards under (below), so a query resolving this wrapper by its contextual tag can reach them.
				QuestInstance->LinkedInnerIdentityTag = CurrentAssetIdentityTag;

				QuestInstance->EntryStepTags = CompileGraph(LinkedGraph->QuestlineEdGraph, InnerPrefix, InnerAliasPrefixes, LinkedBoundaryCompletionsByPath, VisitedAssetPaths, &InnerEntryByPath, ResolveResettable(LinkedGraph->GetResettableReplay(), bNodeResettable));

				// Harvest the linked questline's own questline-level rewards onto the root graph under the linked asset's
				// identity, so they deliver/advertise at runtime even though the linked asset is never loaded then.
				HarvestQuestlineRewards(LinkedGraph, RootGraph, LinkedAssetIdentityName);

				CurrentAssetIdentityTag = PreviousAssetIdentity;
				CurrentInnerContainerTag = PreviousContainer;

				QuestInstance->EntryStepTagsByPath = MoveTemp(InnerEntryByPath);

				CurrentOuterGuidChain = PreviousGuidChain;
				VisitedAssetPaths.RemoveSingleSwap(LinkedPath);
				
				Instance = QuestInstance;
			}
		}
        
    	if (!Instance) continue;

    	Instance->QuestContentGuid = CombineGuids(CurrentOuterGuidChain, ContentNode->QuestGuid);
    	Instance->AuthoredNodeGuid = ContentNode->QuestGuid;
    	Instance->NodeInfo.DisplayName = RehomeDisplayText(ContentNode->NodeLabel, Instance);
    	Instance->bResettableReplay = bNodeResettable;

    	// For LinkedQuestline nodes, fall back per-field to the inner asset's class defaults when the
    	// outer node leaves the corresponding field empty/null. Outer overrides where authored, inner
    	// fills the gap. Designers can rely on inner being the project-wide default for the questline
    	// while still per-instance-overriding specific fields per context.
    	if (UQuestlineNode_LinkedQuestline* LinkedNode = Cast<UQuestlineNode_LinkedQuestline>(ContentNode))
    	{
    		UQuestlineGraph* InnerAsset = LinkedNode->LinkedGraph.LoadSynchronous();
    		Instance->DisplayName = RehomeDisplayText((LinkedNode->DisplayName.IsEmpty() && InnerAsset) ? InnerAsset->DisplayName : LinkedNode->DisplayName, Instance);
    		Instance->Description = RehomeDisplayText((LinkedNode->Description.IsEmpty() && InnerAsset) ? InnerAsset->Description : LinkedNode->Description, Instance);
    		Instance->DisplayData = (!LinkedNode->DisplayData && InnerAsset) ? InnerAsset->DisplayData : LinkedNode->DisplayData;
    	}
    	
        AllCompiledQuestTags.Add(TagName);
    	for (const FName& AliasFName : AliasFNames)
    	{
    		AllCompiledQuestTags.AddUnique(AliasFName);
    	}
    	if (!AliasFNames.IsEmpty())
    	{
    		CompiledAliasFNamesByContextualTag.Add(TagName, AliasFNames);
    	}

    	// Containment tracking — record the just-registered instance under whatever wrapper is currently being
    	// expanded. NAME_None means root level (no enclosing wrapper).
    	if (CurrentInnerContainerTag != NAME_None)
    	{
    		ImmediateContainerByTag.Add(TagName, CurrentInnerContainerTag);
    	}

        // Register path identities: both the raw outcome tag (when path is static) and the per-node path fact
    	if (const UQuestlineNode_Step* QuestStepNode = Cast<UQuestlineNode_Step>(ContentNode))
    	{
    		if (!QuestStepNode->ObjectiveClass.IsNull())
    		{
    			TArray<FObjectivePathDescriptor> Paths =
					FSimpleQuestEditorUtilities::DiscoverObjectivePaths(QuestStepNode->ObjectiveClass.LoadSynchronous());
    			for (const FObjectivePathDescriptor& Desc : Paths)
    			{
    				// Static (registered-tag) identities re-added to AllCompiledQuestTags so they remain picker-
    				// visible across compiles. Dynamic (bare designer / auto-numbered) identities are NOT added.
    				// They aren't registered FGameplayTag names, and would land at the root of the tag manager
    				// where the INI write would auto-decorate them with state-fact suffixes (Live, Completed,
    				// etc.). Provenance is captured at the source by DiscoverObjectivePaths via the K2 node's
    				// ResolvePathIdentity out-param, so a designer typing a dotted PathName cannot defeat it.
    				if (Desc.IsRegisteredTag())
    				{
    					AllCompiledQuestTags.AddUnique(Desc.Identity);
    				}
    				else
    				{
    					UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("CompileNodeRegistration[%s]: skipping bare PathIdentity '%s' "
							"(dynamic — not registering at tag manager root)"), *TagName.ToString(), *Desc.Identity.ToString());
    				}
    				// The path-fact tag is always properly namespaced (SimpleQuest.State.<Quest>.Path.<PathID>),
    				// so registering it never causes pollution regardless of static / dynamic provenance.
    				AllCompiledQuestTags.AddUnique(FQuestTagComposer::MakeNodePathFact(TagName, Desc.Identity));
    			}
    		}
    	}
        
        AllCompiledNodes.Add(TagName, Instance);
        AllCompiledEditorNodes.Add(TagName, ContentNode);
        OutNodeInstanceMap.Add(ContentNode, Instance);
    }
}

void FQuestlineGraphCompiler::CompileGroupSetters(UEdGraph* Graph, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_CompileGroupSetters);
	
    // ---- Prerequisite Rule Entries: compile each Entry's Enter-pin expression subtree into a runtime Monitor ----
	TMap<FGameplayTag, TArray<UQuestlineNode_PrerequisiteRuleEntry*>> PrereqEntriesByTag;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
	    if (UQuestlineNode_PrerequisiteRuleEntry* Entry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(Node))
	    {
	        if (!Entry->GroupTag.IsValid())
	        {
	            AddWarning(FString::Printf(TEXT("[%s] A Prerequisite Rule Entry has no rule tag set and will be skipped."), *TagPrefix), Entry);
	            continue;
	        }
	        PrereqEntriesByTag.FindOrAdd(Entry->GroupTag).Add(Entry);
	    }
	}

	for (auto& [RuleTag, Entries] : PrereqEntriesByTag)
	{
		// Duplicate-tag detection: one Entry per tag is the contract. Multiple Entries would create a silent race where only
		// the first-compiled definition takes effect. Emit a tokenized error with clickable navigation to each offending Entry.
		if (Entries.Num() > 1)
		{
			TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Error);
			Msg->AddToken(FTextToken::Create(FText::FromString(
				FString::Printf(TEXT("[%s] Prerequisite Rule tag '%s' is defined by %d Entries — rule names must be unique. Offending Entries:"),
					*TagPrefix, *RuleTag.GetTagName().ToString(), Entries.Num()))));

			for (UQuestlineNode_PrerequisiteRuleEntry* OffendingEntry : Entries)
			{
				AddNodeNavigationToken(Msg, OffendingEntry);
			}

			Messages.Add(Msg);
			bHasErrors = true;
			NumErrors++;
			UE_LOG(LogSimpleQuestCompiler, Error,
				TEXT("QuestlineGraphCompiler: Prerequisite Rule tag '%s' has %d Entries — rule names must be unique."),
				*RuleTag.GetTagName().ToString(), Entries.Num());

			continue;  // Skip Monitor creation for this tag — asset is in error state.
		}
		
	    UQuestPrereqRuleNode* Monitor = NewObject<UQuestPrereqRuleNode>(RootGraph);
	    Monitor->GroupTag = RuleTag;
		Monitor->AuthoredNodeGuid = Entries[0]->QuestGuid;

	    UQuestlineNode_PrerequisiteRuleEntry* PrimaryEntry = Entries[0];
	    if (Entries.Num() > 1)
	    {
	        UE_LOG(LogSimpleQuestCompiler, Verbose,
	            TEXT("CompileGroupSetters: [%s] rule tag '%s' has %d Entries — using first; duplicate detection pass will error in 4.c."),
	            *TagPrefix, *RuleTag.GetTagName().ToString(), Entries.Num());
	    }

	    if (UEdGraphPin* EnterPin = PrimaryEntry->GetPinByRole(EQuestPinRole::PrereqIn))
	    {
	        if (EnterPin->LinkedTo.Num() > 0)
	        {
	            Monitor->Expression = CompilePrerequisiteExpression(EnterPin, TagPrefix, VisitedAssetPaths);
	        }
	    }

	    const FName RuleTagName = RuleTag.GetTagName();
	    AllCompiledNodes.Add(RuleTagName, Monitor);
		
		// No entry-tag registration: rule monitors subscribe at instance lifetime via OnRegisteredWithManager,
		// not at entry-tag fire time. Mirrors UActivationGroupListenerNode's always-armed semantic.
		
	    TArray<FGameplayTag> LeafTags;
	    Monitor->Expression.CollectLeafTags(LeafTags);
	    UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("CompileGroupSetters: [%s] prereq rule '%s' — expression with %d leaf(s)"),
	        *TagPrefix, *RuleTagName.ToString(), LeafTags.Num());
	}

    // ---- Activation Group Entries: each Entry publishes a tag when its Activate input arrives; compile to runtime instance ----
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ActivationGroupEntry* Setter = Cast<UQuestlineNode_ActivationGroupEntry>(Node);
        if (!Setter) continue;

    	if (!Setter->GroupTag.IsValid())
    	{
    		AddWarning(FString::Printf(TEXT("[%s] An Activation Group Setter has no GroupTag set and will be skipped."), *TagPrefix), Setter);
    		continue;
    	}

    	CompiledSetterGroupTags.Add(Setter->GroupTag);

    	UActivationGroupSetterNode* Inst = NewObject<UActivationGroupSetterNode>(RootGraph);
    	Inst->GroupTag = Setter->GroupTag;
    	Inst->AuthoredNodeGuid = Setter->QuestGuid;

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s__%s"), *Node->NodeGuid.ToString(), *TagPrefix));
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Inst);
        AllCompiledEditorNodes.Add(UtilKey, Node);

    	UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("CompileGroupSetters: [%s] activation setter '%s' — key='%s'"),
			*TagPrefix,
			*Setter->GroupTag.GetTagName().ToString(),
			*UtilKey.ToString());
    }

    // ---- Activation Group Exits: source nodes — subscribe to the group tag, add to entry tags for graph-start activation ----
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ActivationGroupExit* Getter = Cast<UQuestlineNode_ActivationGroupExit>(Node);
        if (!Getter) continue;

        if (!Getter->GroupTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] An Activation Group Getter has no GroupTag set and will be skipped."), *TagPrefix), Getter);
            continue;
        }

    	CompiledListenerGroupTags.Add(Getter->GroupTag);

    	UActivationGroupListenerNode* Inst = NewObject<UActivationGroupListenerNode>(RootGraph);
    	Inst->GroupTag = Getter->GroupTag;
    	Inst->AuthoredNodeGuid = Getter->QuestGuid;

    	const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s__%s"), *Node->NodeGuid.ToString(), *TagPrefix));
    	UtilityNodeKeyMap.Add(Node, UtilKey);
    	AllCompiledNodes.Add(UtilKey, Inst);
    	AllCompiledEditorNodes.Add(UtilKey, Node);

    	// Listeners are NOT registered as graph entry routes — subscription happens at instance lifetime via
    	// OnRegisteredWithManager (the always-armed semantic). The previous OutGetterEntryTags.Add line caused
    	// wrapper-Listener loop bugs by re-firing ActivateInternal each wrapper iteration; the signal path
    	// handles activation cleanly without that registration.

    	UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("CompileGroupSetters: [%s] activation listener '%s' — key='%s'"),
			*TagPrefix,
			*Getter->GroupTag.GetTagName().ToString(),
			*UtilKey.ToString());
    }
}

void FQuestlineGraphCompiler::CompileUtilityNodes(
	UEdGraph* Graph,
	const FString& TagPrefix,
	TArray<FString>& VisitedAssetPaths,
	TArray<UQuestlineNode_UtilityBase*>& OutUtilityEdNodes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_CompileUtilityNodes);

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_UtilityBase* UtilEdNode = Cast<UQuestlineNode_UtilityBase>(Node);
        if (!UtilEdNode) continue;

        UQuestNodeBase* Instance = nullptr;

    	if (UQuestlineNode_SetBlocked* BlockNode = Cast<UQuestlineNode_SetBlocked>(UtilEdNode))
    	{
    		USetBlockedNode* Inst = NewObject<USetBlockedNode>(RootGraph);
    		Inst->TargetQuestTags = BlockNode->TargetQuestTags;
    		Inst->bAlsoDeactivateTargets = BlockNode->bAlsoDeactivateTargets;
    		Inst->AuthoredNodeGuid = BlockNode->QuestGuid;
    		Instance = Inst;
    	}
    	else if (UQuestlineNode_ClearBlocked* ClearBlockNode = Cast<UQuestlineNode_ClearBlocked>(UtilEdNode))
    	{
    		UClearBlockedNode* Inst = NewObject<UClearBlockedNode>(RootGraph);
    		Inst->TargetQuestTags = ClearBlockNode->TargetQuestTags;
    		Inst->AuthoredNodeGuid = ClearBlockNode->QuestGuid;
    		Instance = Inst;
    	}
        else if (UQuestlineNode_StartQuestline* StartNode = Cast<UQuestlineNode_StartQuestline>(UtilEdNode))
        {
            // "Direct from Entry" traversal — Knot reroute nodes are transparent. The cascade flows through
            // knots without gating, so Entry → Knot → Knot → ... → Start Questline is the same cycle as
            // Entry → Start Questline. Any non-knot upstream node would supply its own lifecycle guard on
            // the recursion attempt.
            bool bDirectFromEntry = false;
            TSet<const UEdGraphNode*> VisitedKnots;
            for (const UEdGraphPin* InputPin : UtilEdNode->Pins)
            {
                if (!InputPin || InputPin->Direction != EGPD_Input) continue;
                if (DoesPinReachEntryThroughKnots(InputPin, VisitedKnots))
                {
                    bDirectFromEntry = true;
                    break;
                }
            }

            if (bDirectFromEntry && !StartNode->Graph.IsNull() && RootGraph)
            {
                const FTopLevelAssetPath StartTargetPath = StartNode->Graph.ToSoftObjectPath().GetAssetPath();
                const FTopLevelAssetPath RootGraphPath = FSoftObjectPath(RootGraph).GetAssetPath();
                if (StartTargetPath == RootGraphPath)
                {
                    TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Error);
                    Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(
                        TEXT("[%s] Start Questline node is wired directly from Entry and targets the same questline — activation would cycle indefinitely. Insert a Step between Entry and this node, or point this Start Questline at a different questline."),
                        *TagPrefix))));
                    AddNodeNavigationToken(Msg, UtilEdNode);
                    Messages.Add(Msg);
                    bHasErrors = true;
                    NumErrors++;
                    UE_LOG(LogSimpleQuestCompiler, Error,
                        TEXT("QuestlineGraphCompiler: '%s' has a Start Questline node directly downstream of Entry targeting its own graph — would cycle at runtime."),
                        *RootGraph->GetName());
                    continue;  // Skip runtime instance creation — asset is in error state.
                }
            }

            UStartQuestlineNode* Inst = NewObject<UStartQuestlineNode>(RootGraph);
            Inst->Graph = StartNode->Graph;
            Inst->Params = StartNode->Params;
            Inst->AuthoredNodeGuid = StartNode->QuestGuid;
            Instance = Inst;
        }
        else if (UQuestlineNode_PrereqGate* GateNode = Cast<UQuestlineNode_PrereqGate>(UtilEdNode))
        {
            UPrereqGateNode* Inst = NewObject<UPrereqGateNode>(RootGraph);
            Inst->AuthoredNodeGuid = GateNode->QuestGuid;
            Instance = Inst;
        }
        else if (UQuestlineNode_AddFact* AddFactNode = Cast<UQuestlineNode_AddFact>(UtilEdNode))
        {
        	UAddFactNode* Inst = NewObject<UAddFactNode>(RootGraph);
        	Inst->Facts = AddFactNode->Facts;
        	Inst->BroadcastMode = AddFactNode->BroadcastMode;
        	Inst->AuthoredNodeGuid = AddFactNode->QuestGuid;
        	Instance = Inst;
        }
        else if (UQuestlineNode_RemoveFact* RemoveFactNode = Cast<UQuestlineNode_RemoveFact>(UtilEdNode))
        {
        	URemoveFactNode* Inst = NewObject<URemoveFactNode>(RootGraph);
        	Inst->Facts = RemoveFactNode->Facts;
        	Inst->BroadcastMode = RemoveFactNode->BroadcastMode;
        	Inst->AuthoredNodeGuid = RemoveFactNode->QuestGuid;
        	Instance = Inst;
        }
        else if (UQuestlineNode_ClearFact* ClearFactNode = Cast<UQuestlineNode_ClearFact>(UtilEdNode))
        {
        	UClearFactNode* Inst = NewObject<UClearFactNode>(RootGraph);
        	Inst->Facts = ClearFactNode->Facts;
        	Inst->bSuppressBroadcast = ClearFactNode->bSuppressBroadcast;
        	Inst->AuthoredNodeGuid = ClearFactNode->QuestGuid;
        	Instance = Inst;
        }

        else if (UQuestlineNode_Reward* RewardEdNode = Cast<UQuestlineNode_Reward>(UtilEdNode))
        {
        	UQuestRewardNode* Inst = NewObject<UQuestRewardNode>(RootGraph);
        	// Deep-copy each authored reward as a sub-object of the runtime node so every placement gets its own
        	// instances (per-placement isolation for future escrow state). A shallow assign would share the editor
        	// node's sub-objects. Null array slots are preserved as null and skipped at runtime.
        	Inst->Rewards.Reserve(RewardEdNode->Rewards.Num());
        	for (int32 RewardIndex = 0; RewardIndex < RewardEdNode->Rewards.Num(); ++RewardIndex)
        	{
        		// Named from the authored position rather than left to auto-numbering, which draws from a counter that keeps
        		// advancing and so renames every reward on every compile. Position is the right identity here: reordering
        		// authored rewards IS an edit, and should change the asset.
        		const TObjectPtr<UQuestRewardBase>& Authored = RewardEdNode->Rewards[RewardIndex];
        		Inst->Rewards.Add(Authored
					? DuplicateObject<UQuestRewardBase>(Authored, Inst, *FString::Printf(TEXT("Reward_%d"), RewardIndex))
					: nullptr);
        	}
        	Inst->AuthoredNodeGuid = RewardEdNode->QuestGuid;
        	Instance = Inst;
        }

        if (!Instance) continue;

    	// Give utility nodes the same stable per-placement save identity content nodes get, so save/load can key their
    	// state — a prereq-deferred Prereq Gate has no contextual tag, so QuestContentGuid is its only handle.
    	Instance->QuestContentGuid = CombineGuids(CurrentOuterGuidChain, Instance->AuthoredNodeGuid);
    	
        // Utility nodes with a Prerequisites input pin get the same prereq expression compilation as content nodes.
        // PrereqGate is the first user; future utility nodes opt in by exposing a QuestPrerequisite-category PrereqIn pin.
        if (UEdGraphPin* PrereqPin = UtilEdNode->GetPinByRole(EQuestPinRole::PrereqIn))
        {
            if (PrereqPin->LinkedTo.Num() > 0)
            {
                Instance->PrerequisiteExpression = CompilePrerequisiteExpression(PrereqPin, TagPrefix, VisitedAssetPaths);
            }
        }

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s__%s"), *Node->NodeGuid.ToString(), *TagPrefix));
        OutUtilityEdNodes.Add(UtilEdNode);
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Instance);
        AllCompiledEditorNodes.Add(UtilKey, Node);
    }
}

void FQuestlineGraphCompiler::CompileOutputWiring(
	const TArray<UQuestlineNode_ContentBase*>& ContentNodes,
	const TMap<UQuestlineNode_ContentBase*,
	UQuestNodeBase*>& NodeInstanceMap,
	const FString& TagPrefix,
	const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
	TArray<FString>& VisitedAssetPaths)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_CompileOutputWiring);

	for (UQuestlineNode_ContentBase* ContentNode : ContentNodes)
	{
		UQuestNodeBase* Instance = NodeInstanceMap.FindRef(ContentNode);
		if (!Instance) continue;

		// Prereq compilation runs for ALL content nodes, including LinkedQuestlines. Without this, prereqs
		// wired to a LinkedQuestline node's PrereqIn pin are silently dropped — the runtime instance is left
		// with a default-constructed PrerequisiteExpression that IsAlways() returns true for, and the
		// gating check in QuestManagerSubsystem short-circuits. Lifted above the LinkedQuestline skip below
		// (which exists to bypass output-wiring logic that depends on the node's own title formula —
		// LinkedQuestlines compute their label from the linked asset instead).
		if (UEdGraphPin* PrereqPin = ContentNode->GetPinByRole(EQuestPinRole::PrereqIn))
		{
			if (PrereqPin->LinkedTo.Num() > 0)
			{
				Instance->PrerequisiteExpression = CompilePrerequisiteExpression(PrereqPin, TagPrefix, VisitedAssetPaths);
			}
		}

		// Output wiring runs for ALL content nodes including LinkedQuestlines. The wrapper's NextNodesByPath /
		// NextNodesOnAnyOutcome populated here drive ChainToNextNodes when FireWrapperBoundaryCompletion fires
		// the wrapper post-inner-cascade — without these, downstream nodes wired to the Linked-Questline's
		// outcome pins in the parent graph never activate. The earlier ComputeInnerBoundaryMaps call in
		// CompileNodeRegistration's LinkedQuestline branch builds the boundary-completion map for the INNER
		// graph's compile context (used by inner Steps to populate their BoundaryCompletionsOnForward) — that's
		// orthogonal to this loop, which builds the LinkedQuestline NodeInstance's own outer-graph routing.

		Instance->NextNodesByPath.Empty();
        Instance->NextNodesOnAnyOutcome.Empty();
		Instance->BoundaryCompletionsOnAnyOutcome.Empty();
        Instance->NextNodesOnDeactivation.Empty();
        Instance->NextNodesToDeactivateOnDeactivation.Empty();

    	/**
		 * Source tag for this content node, reconstructed from the compile-time label formula. LinkedQuestlines
		 * are already `continue`d past at the top of this loop, so GetNodeTitle-based labeling is the right choice for
		 * everything that reaches here (Quest, Step, etc.).
		 */
    	const FName SourceTag = MakeNodeTagName(TagPrefix, SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
    	
        // Route each output pin into the correct runtime routing set
        for (UEdGraphPin* Pin : ContentNode->Pins)
        {
            if (Pin->Direction != EGPD_Output) continue;
            if (Pin->bOrphanedPin) continue; 

            // Deactivated pin: split routing by destination pin category
            if (Pin->PinType.PinCategory == TEXT("QuestDeactivated"))
            {
                TArray<FName> ActivateTags, DeactivateTags;
                ResolveDeactivatedPinToTags(Pin, TagPrefix, VisitedAssetPaths, ActivateTags, DeactivateTags);
                for (const FName& Tag : ActivateTags)  Instance->NextNodesOnDeactivation.Add(Tag);
                for (const FName& Tag : DeactivateTags) Instance->NextNodesToDeactivateOnDeactivation.Add(Tag);
                continue;
            }

        	TArray<FName> ResolvedTags;
        	TArray<FQuestBoundaryCompletion> ResolvedBoundaryCompletions;
        	TArray<FQuestGraphResolution> ResolvedGraphs;
        	TMap<FName, TArray<TWeakObjectPtr<const UEdGraphNode>>> VisitedExitsByPath;
        	ResolvePinToTags(Pin, TagPrefix, BoundaryCompletionsByPath, VisitedAssetPaths, ResolvedTags, ResolvedBoundaryCompletions, &VisitedExitsByPath, &ResolvedGraphs);

        	// Duplicate-path-routing check: one outcome pin reaching multiple distinct Outcome terminals that share
        	// a path identity is almost always an authoring mistake. The compiler accepts the union of their destinations
        	// (each Exit's BoundaryTags are independently merged into ResolvedTags), but the authoring intent is
        	// ambiguous — each path should route through exactly one terminal.
        	for (const auto& Pair : VisitedExitsByPath)
        	{
        		if (Pair.Value.Num() > 1)
        		{
        			EmitDuplicateOutcomeRoutingWarning(ContentNode, Pin, Pair.Key, Pair.Value, TagPrefix);
        		}
        	}
        	
        	// Allow boundary completions and graph-exit attributions through even when no Activate destinations
        	// resolved. Common when the LinkedQuestline's outer-side outcome pin only feeds a prereq expression
        	// (no QuestActivation wire), and for the outermost root-asset Exit case where the chain has no
        	// downstream destinations at all but still needs the asset-resolution publish to fire.
        	if (ResolvedTags.IsEmpty() && ResolvedBoundaryCompletions.IsEmpty() && ResolvedGraphs.IsEmpty()) continue;
        	
        	if (Pin->PinType.PinCategory == TEXT("QuestOutcome"))
        	{
        		// PinName is the path identity. No FGameplayTag round-trip needed, the FName is the routing key.
        		FQuestPathNodeList& List = Instance->NextNodesByPath.FindOrAdd(Pin->PinName);
        		for (const FName& Tag : ResolvedTags)
        		{
        			List.NodeTags.AddUnique(Tag);
        		}
        		// Append the boundary completions accumulated by the walk into this path's parallel array.
        		// Cascade order (innermost-first) is preserved by ResolvePinToTags's accumulation pattern, so
        		// ChainToNextNodes fires nested boundaries in the correct semantic order at runtime.
        		for (const FQuestBoundaryCompletion& BC : ResolvedBoundaryCompletions)
        		{
        			List.BoundaryCompletions.AddUnique(BC);
        		}
        		// Parallel append for graph-resolution attributions — each entry causes ChainToNextNodes to
        		// publish a resolution on that asset's identity tag (with the Exit's authored OutcomeTag),
        		// inner-first before BoundaryCompletions fire.
        		for (const FQuestGraphResolution& Resolution : ResolvedGraphs)
        		{
        			List.ResolvedGraphs.AddUnique(Resolution);
        		}
        		// Record per-destination direct reach for (source, specific-path).
        		const FSourcePathKey Key{ SourceTag, Pin->PinName };
        		for (const FName& Tag : ResolvedTags)
        		{
        			DirectReachesByDest.FindOrAdd(Tag).Add(Key);
        		}
        	}
        	else if (UQuestlineNodeBase::GetPinRoleOf(Pin) == EQuestPinRole::AnyOutcomeOut)
        	{
        		for (const FName& Tag : ResolvedTags)
        		{
        			Instance->NextNodesOnAnyOutcome.Add(Tag);
        		}
        		// Same pattern for the Any-Outcome path's boundary completions array on the instance.
        		for (const FQuestBoundaryCompletion& BC : ResolvedBoundaryCompletions)
        		{
        			Instance->BoundaryCompletionsOnAnyOutcome.AddUnique(BC);
        		}
        		// Parallel append for Any-Outcome graph-resolution attributions.
        		for (const FQuestGraphResolution& Resolution : ResolvedGraphs)
        		{
        			Instance->ResolvedGraphsOnAnyOutcome.AddUnique(Resolution);
        		}
        		// Record per-destination direct reach for (source, any-path). NAME_None encodes "any path from
        		// this source" — collision test absorbs specific-path keys from the same source.
        		const FSourcePathKey Key{ SourceTag, NAME_None };
        		for (const FName& Tag : ResolvedTags) DirectReachesByDest.FindOrAdd(Tag).Add(Key);
        	}        	
        }

        // Entry Deactivated pin: merge inner Entry node's deactivation routing into this Quest instance
        if (UQuestlineNode_Quest* QuestEdNode = Cast<UQuestlineNode_Quest>(ContentNode))
        {
            if (UEdGraph* InnerGraph = QuestEdNode->GetInnerGraph())
            {
                for (UEdGraphNode* InnerNode : InnerGraph->Nodes)
                {
                    if (UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(InnerNode))
                    {
						if (UEdGraphPin* DeactivatedPin = EntryNode->GetPinByRole(EQuestPinRole::DeactivatedOut))
                        {
                            if (!DeactivatedPin->bOrphanedPin && DeactivatedPin->LinkedTo.Num() > 0)
                            {
                                const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                                const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
                                TArray<FName> ActivateTags, DeactivateTags;
                                ResolveDeactivatedPinToTags(DeactivatedPin, InnerPrefix, VisitedAssetPaths, ActivateTags, DeactivateTags);
                                for (const FName& Tag : ActivateTags)  Instance->NextNodesOnDeactivation.Add(Tag);
                                for (const FName& Tag : DeactivateTags) Instance->NextNodesToDeactivateOnDeactivation.Add(Tag);
                            }
                        }
                        break;
                    }
                }
            }
        }
        
        // Mark nodes whose output chain reaches an exit — they complete their parent graph
        {
            auto CheckExit = [this](UEdGraphPin* Pin) -> bool
            {
                TSet<const UEdGraphNode*> V;
                return TraversalPolicy->HasDownstreamExit(Pin, V);
            };
            bool bCompletesParent = false;
            for (UEdGraphPin* Pin : ContentNode->Pins)
            {
                if (Pin->Direction == EGPD_Output && !Pin->bOrphanedPin && CheckExit(Pin))
                {
                    bCompletesParent = true;
                    break;
                }
            }
            Instance->bCompletesParentGraph = bCompletesParent;
        }
    }
}

TArray<FName> FQuestlineGraphCompiler::ResolveEntryTags(
	UEdGraph* Graph,
	const FString& TagPrefix,
	const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
	TArray<FString>& VisitedAssetPaths,
	TMap <FName, FQuestEntryRouteList>* OutEntryTagsByPath)
{
	TArray<FName> EntryTags;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(Node);
		if (!EntryNode) continue;

		TArray<FQuestBoundaryCompletion> UnusedBoundaryCompletions;

		/**
		 * Non-outcome output pins (Entered sentinel, Deactivated) produce unconditional entry tags. No per-path or per-source
		 * routing, just "fire when this graph enters regardless of context."
		 */
		for (UEdGraphPin* Pin : Node->Pins)
		{
		    if (Pin->Direction != EGPD_Output) continue;
		    if (Pin->bOrphanedPin) continue;
		    if (Pin->PinType.PinCategory == TEXT("QuestOutcome")) continue;

		    TArray<FName> PinDests;
		    ResolvePinToTags(Pin, TagPrefix, BoundaryCompletionsByPath, VisitedAssetPaths, PinDests, UnusedBoundaryCompletions);
		    EntryTags.Append(PinDests);

		    /**
		     * The Entered pin represents "any parent source that activates this Entry's containing boundary." Semantically
		     * source-abstracted — symmetric to content-node AnyOutcome which is outcome-abstracted. Enumerate each parent source
		     * reaching this graph's boundary and record (sourceTag, outcomeTag) to destTag as a direct reach. ParallelPathKeysCollide
		     * handles AnyOutcome absorption on each enumerated source. Filter by VisitedAssetPaths so AR-scan results from outside
		     * the current compile tree (unrelated top-level assets that happen to link this graph) don't contaminate the analysis.
		     */
			if (UQuestlineNodeBase::GetPinRoleOf(Pin) == EQuestPinRole::AnyOutcomeOut && PinDests.Num() > 0)
		    {
		        TSet<FQuestEffectiveSource> ReachingSources;
		        FQuestlineGraphTraversalPolicy GraphTraversalPolicy;
		        GraphTraversalPolicy.CollectEntryReachingSources(Graph, ReachingSources);

		        for (const FQuestEffectiveSource& Source : ReachingSources)
		        {
		            if (!Source.Pin) continue;

		            const FString SourceAssetPath = Source.Asset ? Source.Asset->GetPathName() : FString();
		            if (!VisitedAssetPaths.Contains(SourceAssetPath)) continue;

		            const UQuestlineNode_ContentBase* SourceContent = Cast<UQuestlineNode_ContentBase>(Source.Pin->GetOwningNode());
		            if (!SourceContent) continue;

		        	const FName SourceTag = ComputeCompiledTagForContentNode(SourceContent, Source.Asset);
		        	if (SourceTag.IsNone()) continue;

		        	// Source.Pin->PinName is the path identity for QuestOutcome pins (registered tag's full FName for
		        	// static placements). For Any-Outcome pins we leave PathIdentity = NAME_None to encode
		        	// "any path from this source". Absorption handled by the collision test.
		        	FName PathIdentity = NAME_None;
		        	if (Source.Pin->PinType.PinCategory == TEXT("QuestOutcome"))
		        	{
		        		PathIdentity = Source.Pin->PinName;
		        	}

		        	const FSourcePathKey Key{ SourceTag, PathIdentity };
		        	for (const FName& DestTag : PinDests)
		        	{
		        		DirectReachesByDest.FindOrAdd(DestTag).Add(Key);
		        	}
		        }
		    }
		}

		/**
		 * Per-spec routing for QuestOutcome pins. Iterate IncomingSignals directly — pin names are disambiguated and not
		 * parseable as gameplay tags. Each exposed spec produces one FQuestEntryDestination per resolved downstream tag, each
		 * tagged with the compiled ContextualTag of the source content node as SourceFilter.
		 */
		if (OutEntryTagsByPath)
		{
			const UQuestlineGraph* ChildAsset = FQuestlineGraphTraversalPolicy::ResolveContainingAsset(Graph);
			for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
			{
				if (!Spec.bExposed) continue;
				if (!Spec.SourceNodeGuid.IsValid())
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry has unqualified incoming-signal spec (outcome '%s') — skipped. Re-run Import."),
						*TagPrefix,
						Spec.Outcome.IsValid() ? *Spec.Outcome.ToString() : TEXT("any")),
						EntryNode);
					continue;
				}

				const FName PinName = UQuestlineNode_Entry::BuildDisambiguatedPinName(Spec, EntryNode->IncomingSignals);
				UEdGraphPin* SpecPin = EntryNode->FindPin(PinName, EGPD_Output);
				if (!SpecPin)
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry spec (outcome '%s', source '%s') has no corresponding pin '%s' — skipped."),
						*TagPrefix,
						Spec.Outcome.IsValid() ? *Spec.Outcome.ToString() : TEXT("any"),
						*Spec.CachedSourceLabel,
						*PinName.ToString()),
						EntryNode);
					continue;
				}

				const FName SourceFilter = ResolveSourceFilterTag(Spec, ChildAsset);
				if (SourceFilter.IsNone())
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry spec (outcome '%s', source GUID %s) has unresolvable source — skipped. Re-run Import to refresh, or verify the parent asset is accessible."),
						*TagPrefix,
						Spec.Outcome.IsValid() ? *Spec.Outcome.ToString() : TEXT("any"),
						*Spec.SourceNodeGuid.ToString()),
						EntryNode);
					continue;
				}

				TArray<FName> DestTags;
				ResolvePinToTags(SpecPin, TagPrefix, BoundaryCompletionsByPath, VisitedAssetPaths, DestTags, UnusedBoundaryCompletions);

				/**
				 * Bucket key: specific outcome for specific specs, FGameplayTag() (invalid) for any-outcome specs. The runtime looks
				 * up both the specific bucket (for matching IncomingOutcomeTag) and the invalid bucket (for source-only matches)
				 * when activating entry destinations.
				 */
				FQuestEntryRouteList& RouteList = OutEntryTagsByPath->FindOrAdd(Spec.Outcome.GetTagName());
				for (const FName& DestTag : DestTags)
				{
					FQuestEntryDestination Dest;
					Dest.DestTag = DestTag;
					Dest.SourceFilter = SourceFilter;
					RouteList.Destinations.Add(Dest);
					
					/**
					 * Entry source-qualified routing is a direct signal flow at runtime — the compiled source tag (SourceFilter)
					 * delivers Spec.Outcome to DestTag without any group dispatch. Record alongside content-outcome-pin direct reaches so
					 * cross-asset parallel-path collisions are detectable at analysis time. Spec.Outcome may be invalid for any-outcome-
					 * from-source specs; the collision test absorbs that via ParallelPathKeysCollide.
					 */
					DirectReachesByDest.FindOrAdd(DestTag).Add(FSourcePathKey{ SourceFilter, Spec.Outcome.GetTagName() });
				}
			}
		}
		break;
	}
	return EntryTags;
}

void FQuestlineGraphCompiler::DetectAndRecordTagRenames(UQuestlineGraph* InGraph, const TMap<FGuid, FName>& OldTagsByGuid)
{
    for (const auto& [TagName, NodeInstance] : InGraph->CompiledNodes)
    {
        if (!NodeInstance || !NodeInstance->GetQuestGuid().IsValid()) continue;
        if (const FName* OldTag = OldTagsByGuid.Find(NodeInstance->GetQuestGuid()))
        {
            if (*OldTag != TagName)
            {
                DetectedTagRenames.Add(*OldTag, TagName);
            }
        }
    }

    if (DetectedTagRenames.Num() == 0) return;

	// No persistent ledger: the redirects written from DetectedTagRenames are what actually carries a rename to
	// content that isn't loaded, and they heal on deserialize. A second record of the same renames was kept here for
	// years without a reader, and its stored OLD names are indistinguishable from live ones to anything scanning
	// packages - which is precisely what blocks a spent redirect from ever being provably retirable.
	UE_LOG(LogSimpleQuestCompiler, Display, TEXT("Compiler: %d tag rename(s) detected"), DetectedTagRenames.Num());
	
	// Per-rename detail — walks the new CompiledNodes to recover the GUID and DisplayName of each renamed node so the
	// Output Log identifies exactly which node is drifting. Intended for diagnosing stale or persistent renames where
	// the same tag flips every compile without a designer-visible reason.
	for (const auto& [OldTag, NewTag] : DetectedTagRenames)
	{
		FGuid OffendingGuid;
		FText OffendingDisplayName;
		if (TObjectPtr<UQuestNodeBase>* Found = InGraph->CompiledNodes.Find(NewTag))
		{
			if (UQuestNodeBase* Node = *Found)
			{
				OffendingGuid = Node->GetQuestGuid();
				OffendingDisplayName = Node->GetNodeInfo().DisplayName;
			}
		}
		UE_LOG(LogSimpleQuestCompiler, Display, TEXT("  rename: '%s' -> '%s' (node '%s', GUID %s)"),
			*OldTag.ToString(),
			*NewTag.ToString(),
			*OffendingDisplayName.ToString(),
			*OffendingGuid.ToString(EGuidFormats::Digits));
	}
}


// -------------------------------------------------------------------------------------------------
// ResolvePinToTags - the node traversal engine
// -------------------------------------------------------------------------------------------------

void FQuestlineGraphCompiler::ResolvePinToTags(
	UEdGraphPin* FromPin,
	const FString& TagPrefix,
	const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
	TArray<FString>& VisitedAssetPaths,
	TArray<FName>& OutTags,
	TArray <FQuestBoundaryCompletion>& OutBoundaryCompletions,
	TMap<FName, TArray<TWeakObjectPtr<const UEdGraphNode>>>* OutVisitedExitsByPath,
	TArray<FQuestGraphResolution>* OutResolvedGraphs)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_ResolvePinToTags);
	
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* Node = LinkedPin->GetOwningNode();

        // Knot: pass through to the other side
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
            {
                ResolvePinToTags(KnotOut, TagPrefix, BoundaryCompletionsByPath, VisitedAssetPaths, OutTags, OutBoundaryCompletions, OutVisitedExitsByPath, OutResolvedGraphs);
            }
        }

    	// Exit nodes accumulate boundary completions only. The wrapper's authored downstream
    	// destinations are activated at runtime via FireWrapperBoundaryCompletion, which fires
    	// the wrapper's own ChainToNextNodes when the chain crosses its boundary. Injecting
    	// those destinations into OutTags here too would double-fire every chain that crosses
    	// a wrapper boundary into a destination the wrapper also wires to — most visibly,
    	// loop-back wires fire once per nesting level instead of once per chain.
        else if (const UQuestlineNode_Exit* ExitNode = Cast<UQuestlineNode_Exit>(Node))
        {
        	if (OutVisitedExitsByPath && ExitNode->OutcomeTag.IsValid())
        	{
        		OutVisitedExitsByPath->FindOrAdd(ExitNode->OutcomeTag.GetTagName()).AddUnique(ExitNode);
        	}

        	if (!ExitNode->OutcomeTag.IsValid())
        	{
        		AddWarning(FString::Printf(TEXT("[%s] An exit node has no OutcomeTag set."), *TagPrefix), ExitNode);
        	}

        	// Asset-root-scope Exit detection: when the Exit's owning UEdGraph is the current asset's own
        	// QuestlineEdGraph (not a Quest container's inner graph), this is a terminus for the current
        	// questline asset. Record the asset's identity tag so ChainToNextNodes publishes a graph-scope
        	// resolution at runtime. Quest container Exits are skipped — the container's own resolution
        	// publish (FQuestEndedEvent on the container node) already covers that case.
        	if (OutResolvedGraphs && CurrentAssetIdentityTag.IsValid())
        	{
        		const UEdGraph* OwningGraph = ExitNode->GetGraph();
        		const UQuestlineGraph* OwningAsset = OwningGraph ? Cast<UQuestlineGraph>(OwningGraph->GetOuter()) : nullptr;
        		if (OwningAsset && OwningGraph == OwningAsset->QuestlineEdGraph && ExitNode->OutcomeTag.IsValid())
        		{
        			// The questline resolves with the Exit's authored OutcomeTag — distinct from any cascading path
        			// outcome that led to this Exit. AddUnique on the {GraphTag, OutcomeTag} pair so multiple paths
        			// through the same Exit produce a single attribution; multiple Exits with distinct OutcomeTags
        			// on the same path produce distinct attributions.
        			OutResolvedGraphs->AddUnique(FQuestGraphResolution{CurrentAssetIdentityTag, ExitNode->OutcomeTag});
        		}
        	}

        	// Accumulate the IMMEDIATE wrapper's boundary completion only — bucket[0] is the Exit's own
        	// wrapper (insert-at-front discipline maintained by ComputeInnerBoundaryMaps). Outer-cascade
        	// entries (bucket[1..N]) are picked up by THAT outer wrapper's own pin-walk at compile time
        	// and become its NextNodesByPath BCs; the wrapper-to-wrapper chain at runtime fires them in
        	// innermost-first order. Pulling the entire bucket here would double-cascade — every wrapper
        	// would fire once via the inner chain AND once via the inner Step's direct BC entry.
        	if (const TArray<FQuestBoundaryCompletion>* BoundaryCompletions = BoundaryCompletionsByPath.Find(ExitNode->OutcomeTag.GetTagName()))
        	{
        		if (!BoundaryCompletions->IsEmpty())
        		{
        			OutBoundaryCompletions.AddUnique((*BoundaryCompletions)[0]);
        		}
        	}
        	else if (const TArray<FQuestBoundaryCompletion>* AnyBoundaryCompletions = BoundaryCompletionsByPath.Find(NAME_None))
        	{
        		if (!AnyBoundaryCompletions->IsEmpty())
        		{
        			OutBoundaryCompletions.AddUnique((*AnyBoundaryCompletions)[0]);
        		}
        	}
        }

        // Quest or Step: return the tag assigned during Pass 1
        else if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
        {
            // Only resolve forward chain when wired to an Activate input. Prerequisite and Deactivate inputs are compiled
            // by their own dedicated passes.
            if (LinkedPin->PinType.PinCategory != TEXT("QuestActivation"))
                continue;

            const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            if (!Label.IsEmpty())
            {
                const FName TagName = MakeNodeTagName(TagPrefix, Label);
                if (!TagName.IsNone())
                {
                    OutTags.AddUnique(TagName);
                }
            }
        }
        
        // Utility node: return its GUID-based key so the caller can route into NextNodesOnForward
        else if (const FName* UtilKey = UtilityNodeKeyMap.Find(Node))
        {
            OutTags.AddUnique(*UtilKey);
        }
    }
}


// -------------------------------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------------------------------

FString FQuestlineGraphCompiler::SanitizeTagSegment(const FString& InLabel) const
{
    return FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(InLabel);
}

FName FQuestlineGraphCompiler::MakeNodeTagName(const FString& TagPrefix, const FString& SanitizedLabel) const
{
    return FQuestTagComposer::MakeIdentityTag(TagPrefix, { SanitizedLabel });
}

void FQuestlineGraphCompiler::AddError(const FString& Message, const UEdGraphNode* Node)
{
    bHasErrors = true;
    NumErrors++;
    TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Error, FText::FromString(Message));
    if (Node) AddNodeNavigationToken(Msg, Node);
    Messages.Add(Msg);
    UE_LOG(LogSimpleQuestCompiler, Error, TEXT("QuestlineGraphCompiler: %s"), *Message);
}

void FQuestlineGraphCompiler::AddWarning(const FString& Message, const UEdGraphNode* Node)
{
    NumWarnings++;
    TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning, FText::FromString(Message));
    if (Node) AddNodeNavigationToken(Msg, Node);
    Messages.Add(Msg);
    UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("QuestlineGraphCompiler: %s"), *Message);
}

void FQuestlineGraphCompiler::RegisterCompiledTags(UQuestlineGraph* InGraph)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_RegisterCompiledTags);

	// A transient graph has no persistent identity, so its tags must not reach the project's compiled tag config: the
	// registry is keyed by package name, and every transient graph shares /Engine/Transient. Compiling a scratch graph -
	// an import preview, a tooling fixture, a test - would otherwise write tags into a tracked config that nothing can
	// later attribute or clean up, since the "asset" they came from never existed on disk.
	UPackage* Package = InGraph ? InGraph->GetPackage() : nullptr;
	if (!Package || Package->HasAnyFlags(RF_Transient) || Package == GetTransientPackage()) return;

	ISimpleQuestEditorModule::Get().RegisterCompiledTags(
		Package->GetName(),
		InGraph->CompiledQuestTags);
}

void FQuestlineGraphCompiler::CollectTransitiveParentSources(UEdGraph* InGraph, const TArray<FString>& VisitedAssetPaths, TSet<FSourcePathKey>& OutKeys,	TSet<UEdGraph*>& VisitedGraphs)
{
	if (!InGraph || VisitedGraphs.Contains(InGraph)) return;
	VisitedGraphs.Add(InGraph);

	TSet<FQuestEffectiveSource> LocalSources;
	TraversalPolicy->CollectEntryReachingSources(InGraph, LocalSources);

	for (const FQuestEffectiveSource& Source : LocalSources)
	{
		if (!Source.Pin) continue;

		const FString SourceAssetPath = Source.Asset ? Source.Asset->GetPathName() : FString();
		if (!VisitedAssetPaths.Contains(SourceAssetPath)) continue;

		UEdGraphNode* SourceNode = Source.Pin->GetOwningNode();

		/**
		 * Case A: source is a content-node outcome pin (or Any Outcome). Concrete terminal - record the compiled source tag
		 * and outcome, stop walking this branch.
		 */
		if (const UQuestlineNode_ContentBase* SourceContent = Cast<UQuestlineNode_ContentBase>(SourceNode))
		{
			const FName SourceTag = ComputeCompiledTagForContentNode(SourceContent, Source.Asset);
			if (SourceTag.IsNone()) continue;

			// Source.Pin->PinName is the path identity for QuestOutcome pins; NAME_None for Any-Outcome
			// (parent's "any path" - absorption handles it).
			const FName PathIdentity = (Source.Pin->PinType.PinCategory == TEXT("QuestOutcome"))
				? Source.Pin->PinName
				: NAME_None;

			OutKeys.Add(FSourcePathKey{ SourceTag, PathIdentity });
			continue;
		}

		/**
		 * Case B: source is an Entry pin, transitive continuation.
		 */
		if (const UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(SourceNode))
		{
			// B1: Entered sentinel — climb to this Entry's graph and gather its parent sources recursively.
			if (UQuestlineNodeBase::GetPinRoleOf(Source.Pin) == EQuestPinRole::AnyOutcomeOut)
			{
				CollectTransitiveParentSources(EntryNode->GetGraph(), VisitedAssetPaths, OutKeys, VisitedGraphs);
				continue;
			}

			/**
			 * B2: Source-qualified spec pin. The spec's SourceNodeGuid already encodes the original content source (that's
			 * the whole point of specs), so no recursion needed — resolve the source tag directly and record. Match the pin
			 * to its spec by recomputing the disambiguated pin name.
			 */
			if (Source.Pin->PinType.PinCategory == TEXT("QuestOutcome"))
			{
				for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
				{
					if (!Spec.bExposed) continue;
					if (UQuestlineNode_Entry::BuildDisambiguatedPinName(Spec, EntryNode->IncomingSignals) != Source.Pin->PinName) continue;

					const FName SourceTag = ResolveSourceFilterTag(Spec, Source.Asset);
					if (SourceTag.IsNone()) continue;

					OutKeys.Add(FSourcePathKey{ SourceTag, Spec.Outcome.GetTagName() });
					break;
				}
				continue;
			}
		}

		// Utility/prereq/other nodes as Entry-reaching sources shouldn't occur; if they do, we ignore them.
	}
}

FName FQuestlineGraphCompiler::ComputeCompiledTagForContentNode(const UQuestlineNode_ContentBase* SourceNode, const UQuestlineGraph* ContainingAsset) const
{
	if (!SourceNode || !ContainingAsset) return NAME_None;

	/**
	 * Walk up the Outer chain collecting sanitized labels. A content node either lives directly in the top-level asset graph
	 * or is nested inside one or more Quest node inner graphs. Each level contributes its label to the compiled tag path.
	 */
	TArray<FString> LabelsTopDown;
	const UEdGraphNode* Cursor = SourceNode;
	while (Cursor)
	{
		const UQuestlineNode_ContentBase* CursorContent = Cast<UQuestlineNode_ContentBase>(Cursor);
		if (!CursorContent) break;
		LabelsTopDown.Insert(SanitizeTagSegment(CursorContent->NodeLabel.ToString()), 0);

		const UEdGraph* Graph = Cursor->GetGraph();
		if (!Graph) break;
		UObject* Outer = Graph->GetOuter();
		if (const UQuestlineNode_Quest* ContainerQuest = Cast<UQuestlineNode_Quest>(Outer))
		{
			Cursor = ContainerQuest;
			continue;
		}
		break; // Reached the top-level asset graph.
	}

	if (LabelsTopDown.IsEmpty()) return NAME_None;

	const FString AssetPrefix = SanitizeTagSegment(ContainingAsset->GetQuestlineID().IsEmpty() ? ContainingAsset->GetName() : ContainingAsset->GetQuestlineID());
	return FQuestTagComposer::MakeIdentityTag(AssetPrefix, LabelsTopDown);
}

FName FQuestlineGraphCompiler::ResolveSourceFilterTag(const FIncomingSignalPinSpec& Spec, const UQuestlineGraph* ChildAsset) const
{
	if (!Spec.SourceNodeGuid.IsValid()) return NAME_None;

	/**
	 * Determine which asset contains the source. Same-asset (empty ParentAsset) uses ChildAsset; cross-asset sync-loads the
	 * referenced asset. Then recursively search all graphs in that asset for a content node with matching QuestGuid.
	 */
	UQuestlineGraph* SourceAsset = nullptr;
	if (Spec.ParentAsset.IsNull())
	{
		SourceAsset = const_cast<UQuestlineGraph*>(ChildAsset);
	}
	else
	{
		SourceAsset = Cast<UQuestlineGraph>(Spec.ParentAsset.TryLoad());
	}
	if (!SourceAsset || !SourceAsset->QuestlineEdGraph) return NAME_None;

	TFunction<const UQuestlineNode_ContentBase*(const UEdGraph*)> FindByGuid;
	FindByGuid = [&FindByGuid, &Spec](const UEdGraph* Graph) -> const UQuestlineNode_ContentBase*
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
			{
				if (ContentNode->QuestGuid == Spec.SourceNodeGuid) return ContentNode;
			}
			if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
			{
				if (UEdGraph* InnerGraph = QuestNode->GetInnerGraph())
				{
					if (const UQuestlineNode_ContentBase* Found = FindByGuid(InnerGraph)) return Found;
				}
			}
		}
		return nullptr;
	};

	const UQuestlineNode_ContentBase* SourceNode = FindByGuid(SourceAsset->QuestlineEdGraph);
	if (!SourceNode) return NAME_None;

	return ComputeCompiledTagForContentNode(SourceNode, SourceAsset);
}

int32 FQuestlineGraphCompiler::CompilePrerequisiteFromOutputPin(
	UEdGraphPin* OutputPin,
	const FString& TagPrefix,
	TArray<FString>& VisitedAssetPaths,
	TSet<const UEdGraphNode*>& OnPath,
	FPrerequisiteExpression& OutExpression)
{
	if (!OutputPin) return INDEX_NONE;
	UEdGraphNode* Node = OutputPin->GetOwningNode();
	if (!Node) return INDEX_NONE;

	/**
	 * A RECURSION STACK, not a visited set. A prerequisite OUTPUT may legitimately feed more than one parent, so the
	 * same node can be reached twice by different paths - a shared sub-expression, which must compile into a subtree
	 * at EACH occurrence because the expression is a tree of appended nodes. A permanent visited set would return INDEX_NONE
	 * the second time and SILENTLY DROP a branch, trading a loud crash for a quiet wrong answer. Marking on entry and
	 * clearing on exit separates "seen before" from "seen on the way here", and only the second is a cycle. The schema
	 * refuses most cycles at authoring time but not all of them, and this walk must not depend on that: a graph can also
	 * arrive from the resolver, from a hand-edited asset, or from an authoring path added later.
	 */
	if (OnPath.Contains(Node))
	{
		AddError(FString::Printf(TEXT("[%s] Circular prerequisite wiring at '%s' - a prerequisite chain cannot feed back into itself."),
								 *TagPrefix, *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()), Node);
		return INDEX_NONE;
	}
	OnPath.Add(Node);
	ON_SCOPE_EXIT { OnPath.Remove(Node); };

    if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
    {
        UEdGraphPin* KnotIn = Knot->FindPin(TEXT("KnotIn"), EGPD_Input);
        if (KnotIn && KnotIn->LinkedTo.Num() > 0)
        {
            return CompilePrerequisiteFromOutputPin(KnotIn->LinkedTo[0], TagPrefix, VisitedAssetPaths, OnPath, OutExpression);
        }
        return INDEX_NONE;
    }
    
    // AND
    if (Cast<UQuestlineNode_PrerequisiteAnd>(Node))
    {
        return CompileCombinatorNode(EPrerequisiteExpressionType::And, Node, TagPrefix, VisitedAssetPaths, OnPath, OutExpression);
    }

    // OR
    if (Cast<UQuestlineNode_PrerequisiteOr>(Node))
    {
        return CompileCombinatorNode(EPrerequisiteExpressionType::Or, Node, TagPrefix, VisitedAssetPaths, OnPath, OutExpression);
    }
    
	// NOT
	if (UQuestlineNode_PrerequisiteNot* NotNode = Cast<UQuestlineNode_PrerequisiteNot>(Node))
	{
		const int32 NodeIndex = OutExpression.AddNot();

		if (UEdGraphPin* CondPin = NotNode->GetPinByRole(EQuestPinRole::PrereqIn))
		{
			if (CondPin->LinkedTo.Num() > 0)
			{
				const int32 ChildIndex = CompilePrerequisiteFromOutputPin(CondPin->LinkedTo[0], TagPrefix, VisitedAssetPaths, OnPath, OutExpression);
				OutExpression.AddCombinatorChild(NodeIndex, ChildIndex);
			}
		}
		return NodeIndex;
	}
	
	// Fact Tag leaf: standalone prereq authoring against an arbitrary World State fact tag. Decoupled from
	// graph topology — designer picks any registered gameplay tag and the leaf gates on its presence.
	if (UQuestlineNode_PrerequisiteFactTag* FactTagNode = Cast<UQuestlineNode_PrerequisiteFactTag>(Node))
	{
		if (!FactTagNode->FactTag.IsValid())
		{
			AddWarning(FString::Printf(TEXT("[%s] A Fact Tag prereq node has no FactTag set and will be skipped."), *TagPrefix), FactTagNode);
			return INDEX_NONE;
		}
		return OutExpression.AddFactLeaf(FactTagNode->FactTag);
	}
	
	// Outcome leaf: context-free prereq authoring against an outcome tag. Decoupled from graph topology AND from
	// any specific quest — satisfies when any quest in the session has resolved with the picked outcome (or any
	// descendant). Backed by Phase 6a outcome-channel publishing + HasAnyQuestResolvedWith hierarchy walk.
	if (UQuestlineNode_PrerequisiteOutcome* OutcomeNode = Cast<UQuestlineNode_PrerequisiteOutcome>(Node))
	{
		if (!OutcomeNode->OutcomeTag.IsValid())
		{
			AddWarning(FString::Printf(TEXT("[%s] An Outcome prereq node has no OutcomeTag set and will be skipped."), *TagPrefix), OutcomeNode);
			return INDEX_NONE;
		}
		return OutExpression.AddOutcomeLeaf(OutcomeNode->OutcomeTag);
	}

	// Getter: resolves to a Leaf on the group's Satisfied tag
	if (UQuestlineNode_PrerequisiteRuleExit* Getter = Cast<UQuestlineNode_PrerequisiteRuleExit>(Node))
	{
		if (!Getter->GroupTag.IsValid())
		{
			AddWarning(FString::Printf(TEXT("[%s] A Prereq Group Getter has no GroupTag set and will be skipped."), *TagPrefix), Getter);
			return INDEX_NONE;
		}
		return OutExpression.AddFactLeaf(Getter->GroupTag);
	}

	// Rule Entry Forward: direct-eval. Inline the Enter pin's linked expression subtree so a local Forward
	// consumer avoids the WorldState roundtrip that a cross-graph Exit would use. Behaviorally equivalent
	// to reading the rule's published tag, but evaluated directly without waiting on the Monitor's publish.
	if (UQuestlineNode_PrerequisiteRuleEntry* Entry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(Node))
	{
		if (!Entry->GroupTag.IsValid())
		{
			AddWarning(FString::Printf(TEXT("[%s] A Prerequisite Rule Entry has no rule tag set and will be skipped."), *TagPrefix), Entry);
			return INDEX_NONE;
		}

		if (UEdGraphPin* EnterPin = Entry->GetPinByRole(EQuestPinRole::PrereqIn))
		{
			if (EnterPin->LinkedTo.Num() > 0)
			{
				return CompilePrerequisiteFromOutputPin(EnterPin->LinkedTo[0], TagPrefix, VisitedAssetPaths, OnPath, OutExpression);
			}
		}

		// No wired expression on Enter: fall back to the tag-read leaf. Same expression a cross-graph Exit
		// compiles to; evaluates false at runtime unless the Monitor has somehow published the tag anyway.
		return OutExpression.AddFactLeaf(Entry->GroupTag);
	}
    
    // Entry node: outcome pin to leaf checking entry outcome fact; "Any Outcome" → parent quest Live fact
    if (Cast<UQuestlineNode_Entry>(Node))
    {
    	const FName QuestTagName = FName(*(FQuestTagComposer::IdentityNamespace + TagPrefix));
    	
    	if (UQuestlineNodeBase::GetPinRoleOf(OutputPin) == EQuestPinRole::AnyOutcomeOut)
    	{
    		// The parent quest's Live fact is always set when the inner graph is running
    		const FGameplayTag LiveFactTag = UGameplayTagsManager::Get().RequestGameplayTag(FQuestTagComposer::MakeStateFact(QuestTagName, EQuestStateLeaf::Live), false);
    		return OutExpression.AddFactLeaf(LiveFactTag);
    	}

        const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutputPin->PinName, false);
        if (!OutcomeTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] Entry outcome pin '%s' does not resolve to a valid gameplay tag — prerequisite skipped."),
                                       *TagPrefix, *OutputPin->PinName.ToString()), Node);
            return INDEX_NONE;
        }

        // Warn if used in a top-level graph — entry outcome facts are only written when a Quest node receives an IncomingOutcomeTag
        UObject* GraphOuter = Node->GetGraph() ? Node->GetGraph()->GetOuter() : nullptr;
        if (!Cast<UQuestlineNode_Quest>(GraphOuter))
        {
            AddWarning(FString::Printf(TEXT("[%s] Entry outcome '%s' used as prerequisite in a top-level graph — this fact is only set when a parent Quest node is activated with a matching outcome."),
                                       *TagPrefix, *OutcomeTag.ToString()), Node);
        }

    	// Entry pin prereq: stamps a Leaf_Entry leaf carrying (ContextualTag, OutcomeTag). Runtime evaluation reads the
    	// QuestStateSubsystem entry registry via HasEnteredWith; subscription routes through FQuestEntryRecorded-
    	// Event on the parent quest's tag channel.
    	return OutExpression.AddEntryLeaf(QuestTagName, OutcomeTag);
    }

    
    // Content node: Success/Failure becomes single Leaf; Any Outcome builds OR(Succeeded, Failed)
    if (Cast<UQuestlineNode_ContentBase>(Node))
    {
		if (UQuestlineNodeBase::GetPinRoleOf(OutputPin) == EQuestPinRole::AnyOutcomeOut)
        {
            const UQuestlineNode_ContentBase* CN = Cast<UQuestlineNode_ContentBase>(Node);
            const FString Label = SanitizeTagSegment(CN->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            const FName NodeTagName = MakeNodeTagName(TagPrefix, Label);

            // Collect all named QuestOutcome pins on this node
            TArray<UEdGraphPin*> OutcomePins;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("QuestOutcome"))
                    OutcomePins.Add(Pin);
            }

			// No named outcomes. This node is satisfied by Leaf_Completed alone
			if (OutcomePins.IsEmpty())
			{
				const FGameplayTag CompletedFactTag = UGameplayTagsManager::Get().RequestGameplayTag(FQuestTagComposer::MakeStateFact(NodeTagName, EQuestStateLeaf::Completed), false);
				return OutExpression.AddFactLeaf(CompletedFactTag);
			}

			// Build OR over all named output paths. AnyOutcomeOut is satisfied when ANY of the source node's
			// declared paths fires — each path becomes its own Leaf_Path child.
			const int32 OrIndex = OutExpression.AddCombinator(EPrerequisiteExpressionType::Or);
			
			// Resettable-replay scope is per node, so every path leaf for this node shares the same read-mode.
			const bool bResettable = IsNodeTagResettable(NodeTagName);

			for (UEdGraphPin* OutcomePin : OutcomePins)
			{
				if (OutcomePin->PinName.IsNone()) continue;

				// PinName is the path identity directly. Static K2 placements have PinName equal to the
				// outcome tag's name; dynamic placements supply a designer-authored sanitized PathName.
				// Either shape feeds the leaf as-is — no gameplay-tag parsing required, no silent drop
				// when PinName isn't a registered tag.
				//
				// AddPathLeaf may reallocate Nodes; AddCombinatorChild's parent index is reused after the leaf
				// add so the index stays valid (TArray Add returns by value, no dangling reference into the array).
				const int32 LeafIdx = OutExpression.AddPathLeaf(NodeTagName, OutcomePin->PinName, bResettable);
				OutExpression.AddCombinatorChild(OrIndex, LeafIdx);
			}

			return OrIndex;
        }

    	// Specific path pin (a single named path wired into a prereq input). PinName is the path identity
    	// directly — equal to the outcome tag's name for static K2 placements, or the designer-authored
    	// sanitized PathName for dynamic placements. Either shape feeds Leaf_Path as-is; no gameplay-tag
    	// parsing, no silent drop when PinName isn't a registered tag string. The leaf is satisfied only
    	// when the named quest resolves through THIS specific authored path.
    	const UQuestlineNode_ContentBase* ContentNode = Cast<const UQuestlineNode_ContentBase>(OutputPin->GetOwningNode());
    	if (!ContentNode) return INDEX_NONE;
    	const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    	if (Label.IsEmpty()) return INDEX_NONE;
    	const FName NodeTagName = MakeNodeTagName(TagPrefix, Label);
    	if (OutputPin->PinName.IsNone()) return INDEX_NONE;

    	return OutExpression.AddPathLeaf(NodeTagName, OutputPin->PinName, IsNodeTagResettable(NodeTagName));
    }

    return INDEX_NONE;
}

int32 FQuestlineGraphCompiler::CompileCombinatorNode(
	EPrerequisiteExpressionType Type,
	UEdGraphNode* Node,
	const FString& TagPrefix,
	TArray<FString>& VisitedAssetPaths,
	TSet <const UEdGraphNode*>& OnPath,
	FPrerequisiteExpression& OutExpression)
{
	const int32 NodeIndex = OutExpression.AddCombinator(Type);

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction != EGPD_Input) continue;
        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            const int32 ChildIndex = CompilePrerequisiteFromOutputPin(LinkedPin, TagPrefix, VisitedAssetPaths, OnPath, OutExpression);
            if (ChildIndex != INDEX_NONE)
            {
                OutExpression.Nodes[NodeIndex].ChildIndices.Add(ChildIndex);
            }
        }
    }
    return NodeIndex;
}

void FQuestlineGraphCompiler::ComputeInnerBoundaryMaps(
	UQuestlineNode_ContentBase* ContentNode,
	const FString& TagPrefix,
	const FString& Label,
	const TMap<FName, TArray<FQuestBoundaryCompletion>>& BoundaryCompletionsByPath,
	TArray<FString>& VisitedAssetPaths,
	TMap<FName, TArray<FQuestBoundaryCompletion>>& OutBoundaryCompletionsByPath)
{
	const FName WrapperTagName = MakeNodeTagName(TagPrefix, Label);

	for (UEdGraphPin* OutputPin : ContentNode->Pins)
	{
		if (OutputPin->Direction != EGPD_Output) continue;
		TArray<FName> UnusedPinTags;
		TArray<FQuestBoundaryCompletion> PinBoundaryCompletions;
		ResolvePinToTags(OutputPin, TagPrefix, BoundaryCompletionsByPath, VisitedAssetPaths, UnusedPinTags, PinBoundaryCompletions);

		auto AccumulateForKey = [&](FName Key, FGameplayTag OutcomeTag)
		{
			// Inherited boundary completions from outer-context cascading — append in order so the
			// outer wrappers complete after this one (cascade semantic: innermost-first).
			for (const FQuestBoundaryCompletion& BC : PinBoundaryCompletions)
			{
				OutBoundaryCompletionsByPath.FindOrAdd(Key).AddUnique(BC);
			}

    		// Register the wrapper's Path fact tag so SetQuestResolved at runtime can resolve and write it.
    		// Without this, RequestGameplayTag returns invalid for the path fact and AddFact is a silent
    		// no-op — Completed appears (its tag is auto-registered by the state-fact infrastructure) but
    		// Path.<Outcome> stays missing. Mirrors the Step branch's path-fact registration in
    		// CompileNodeRegistration (lines ~519-521).
    		if (OutcomeTag.IsValid())
    		{
    			AllCompiledQuestTags.AddUnique(FQuestTagComposer::MakeNodePathFact(WrapperTagName, OutcomeTag.GetTagName()));
    		}

    		// This container's own wrapper completion. Insert at front so it fires before any inherited
    		// outer-cascade completions when the inner graph's Exit walk picks this map up — gives nested
    		// containers the correct innermost-first cascade order.
    		FQuestBoundaryCompletion OwnCompletion;
    		OwnCompletion.WrapperTagName = WrapperTagName;
    		OwnCompletion.OutcomeTag = OutcomeTag;
    		TArray<FQuestBoundaryCompletion>& Bucket = OutBoundaryCompletionsByPath.FindOrAdd(Key);
    		if (!Bucket.Contains(OwnCompletion))
    		{
    			Bucket.Insert(OwnCompletion, 0);
    		}
    	};

        if (OutputPin->PinType.PinCategory == TEXT("QuestOutcome"))
        {
            const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutputPin->PinName, false);
            AccumulateForKey(OutputPin->PinName, OutcomeTag);
        }
        else if (UQuestlineNodeBase::GetPinRoleOf(OutputPin) == EQuestPinRole::AnyOutcomeOut)
        {
            AccumulateForKey(NAME_None, FGameplayTag());
        }
    }
}

FGuid FQuestlineGraphCompiler::CombineGuids(const FGuid& Outer, const FGuid& Inner)
{
	if (!Outer.IsValid()) return Inner;
	return FGuid(
		HashCombine(Outer.A, Inner.A),
		HashCombine(Outer.B, Inner.B),
		HashCombine(Outer.C, Inner.C),
		HashCombine(Outer.D, Inner.D));
}

FPrerequisiteExpression FQuestlineGraphCompiler::CompilePrerequisiteExpression(UEdGraphPin* PrerequisiteInputPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths)
{
    FPrerequisiteExpression Expression;
    if (!PrerequisiteInputPin || PrerequisiteInputPin->LinkedTo.IsEmpty()) return Expression;

	// Fresh per root, never shared between expressions - the guard is about one walk's own path, not about what some
	// earlier prerequisite happened to touch.
	TSet<const UEdGraphNode*> OnPath;
    // Schema enforces exactly one wire into any QuestPrerequisite input pin
    const int32 RootIndex = CompilePrerequisiteFromOutputPin(PrerequisiteInputPin->LinkedTo[0], TagPrefix, VisitedAssetPaths, OnPath, Expression);

    if (RootIndex == INDEX_NONE)
    {
        Expression.Nodes.Reset(); // unresolvable — fall back to Always
    }
    else
    {
        Expression.RootIndex = RootIndex;
    }

    return Expression;
}

void FQuestlineGraphCompiler::ResolveDeactivatedPinToTags(
	UEdGraphPin* FromPin,
	const FString& TagPrefix,
	TArray<FString>& VisitedAssetPaths,
	TArray<FName>& OutActivateTags,
	TArray<FName>& OutDeactivateTags)
{
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* Node = LinkedPin->GetOwningNode();

        // Knot: pass through; the output side carries the category context to each destination
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
            {
                ResolveDeactivatedPinToTags(KnotOut, TagPrefix, VisitedAssetPaths, OutActivateTags, OutDeactivateTags);
            }
            continue;
        }

        // Content node: classify by which input pin was connected
        if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
        {
            const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            if (Label.IsEmpty()) continue;
            const FName TagName = MakeNodeTagName(TagPrefix, Label);
            if (TagName.IsNone()) continue;

            if (LinkedPin->PinType.PinCategory == TEXT("QuestActivation"))
            {
                // Deactivated to Activate: activate this node when the source deactivates
                OutActivateTags.AddUnique(TagName);
            }
            else if (LinkedPin->PinType.PinCategory == TEXT("QuestDeactivate"))
            {
                // Deactivated to Deactivate: cascade deactivation to this node
                OutDeactivateTags.AddUnique(TagName);
            }
            continue;
        }

        // Utility node: can only receive Activate, so always goes to OutActivateTags
        if (const FName* UtilKey = UtilityNodeKeyMap.Find(Node))
        {
            OutActivateTags.AddUnique(*UtilKey);
        }
    }
}

void FQuestlineGraphCompiler::AddNodeNavigationToken(TSharedRef<FTokenizedMessage>& Msg, const UEdGraphNode* Node)
{
    TWeakObjectPtr<UEdGraphNode> WeakNode = const_cast<UEdGraphNode*>(Node);

    Msg->AddToken(FActionToken::Create(
        FText::FromString(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()),
        NSLOCTEXT("SimpleQuestEditor", "GoToNode", "Navigate to this node in the graph editor"),
        FOnActionTokenExecuted::CreateLambda([WeakNode]()
        {
            UEdGraphNode* PinnedNode = WeakNode.Get();
            if (!PinnedNode || !PinnedNode->GetGraph()) return;

            // Walk outer chain to find the UQuestlineGraph asset
            UQuestlineGraph* QuestlineGraph = nullptr;
            for (UObject* Outer = PinnedNode->GetGraph(); Outer; Outer = Outer->GetOuter())
            {
                QuestlineGraph = Cast<UQuestlineGraph>(Outer);
                if (QuestlineGraph) break;
            }
            if (!QuestlineGraph || !GEditor) return;

            // Open the asset editor and navigate to the node
            UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            EditorSubsystem->OpenEditorForAsset(QuestlineGraph);

            if (IAssetEditorInstance* EditorInstance = EditorSubsystem->FindEditorForAsset(QuestlineGraph, false))
            {
                static_cast<FQuestlineGraphEditor*>(EditorInstance)->NavigateToLocation(PinnedNode->GetGraph(), PinnedNode);
            }
        })
    ));
}

bool FQuestlineGraphCompiler::ParallelPathKeysCollide(const FSourcePathKey& A, const FSourcePathKey& B)
{
	if (A.SourceTag != B.SourceTag) return false;
	// AnyPath (NAME_None) on either side absorbs the specific path on the other.
	if (A.Path.IsNone() || B.Path.IsNone()) return true;
	return A.Path == B.Path;
}

void FQuestlineGraphCompiler::EmitParallelPathWarnings()
{
	UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("Surface D: %d setter group(s), %d getter group(s), %d direct-reach destination(s)"),
		GroupSetterSourcesByTag.Num(), GroupGetterDestsByTag.Num(), DirectReachesByDest.Num());

	/**
	 * Cross-reference pass. For each group tag that has both setters and getters: for every destination the getters reach,
	 * check if any direct-reach source at that destination collides with any setter source for the group (under AnyOutcome
	 * absorption). Each collision emits one tokenized warning pointing at source, destination, setter, and getter.
	 */
	for (const auto& [GroupTag, GetterDests] : GroupGetterDestsByTag)
	{
		const TSet<FSourcePathKey>* SetterSources = GroupSetterSourcesByTag.Find(GroupTag);
		if (!SetterSources || SetterSources->IsEmpty()) continue;

		for (const FName& DestTag : GetterDests)
		{
			const TSet<FSourcePathKey>* DirectSources = DirectReachesByDest.Find(DestTag);
			if (!DirectSources || DirectSources->IsEmpty()) continue;

			for (const FSourcePathKey& SetterSource : *SetterSources)
			{
				for (const FSourcePathKey& DirectSource : *DirectSources)
				{
					if (!ParallelPathKeysCollide(SetterSource, DirectSource)) continue;
					EmitParallelPathCollisionWarning(GroupTag, SetterSource, DirectSource, DestTag);
				}
			}
		}
	}
}

void FQuestlineGraphCompiler::EmitDuplicateOutcomeRoutingWarning(const UEdGraphNode* SourceNode, const UEdGraphPin* SourcePin,
	const FName& DuplicatedPathIdentity, const TArray<TWeakObjectPtr<const UEdGraphNode>>& DuplicateExits, const FString& TagPrefix)
{
	TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);

	const FString PinDisplay = SourcePin ? SourcePin->PinName.ToString() : TEXT("<unknown pin>");
	Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(
		TEXT("[%s] Output pin '%s' on"), *TagPrefix, *PinDisplay))));

	if (SourceNode) AddNodeNavigationToken(Msg, SourceNode);
	else Msg->AddToken(FTextToken::Create(FText::FromString(TEXT("<unknown source>"))));

	Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(
		TEXT("reaches %d Outcome terminals sharing path identity '%s':"), DuplicateExits.Num(), *DuplicatedPathIdentity.ToString()))));

	for (const TWeakObjectPtr<const UEdGraphNode>& WeakExit : DuplicateExits)
	{
		if (const UEdGraphNode* ExitNode = WeakExit.Get())
		{
			AddNodeNavigationToken(Msg, ExitNode);
		}
	}

	Msg->AddToken(FTextToken::Create(FText::FromString(
		TEXT("(Ambiguous authoring: route each distinct path through a single terminal, or change the terminals' tags to be distinct.)"))));

	Messages.Add(Msg);
	NumWarnings++;

	UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("Duplicate path routing: pin '%s' on '%s' reaches %d terminals on path '%s'"),
		*PinDisplay,
		SourceNode ? *SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("<unknown>"),
		DuplicateExits.Num(),
		*DuplicatedPathIdentity.ToString());
}

void FQuestlineGraphCompiler::EmitParallelPathCollisionWarning(const FGameplayTag& GroupTag, const FSourcePathKey& SetterSource, const FSourcePathKey& DirectSource, const FName& DestTag)
{
	/**
	 * Resolve editor-node refs for the navigation tokens. Source and destination come from the compile-tree-wide editor-node
	 * map keyed by compiled tag. Setter and getter come from the per-group editor-node maps populated during collection. If
	 * any ref is missing, the corresponding slot falls back to a plain-text token showing the tag/name so the message is
	 * still readable and informative.
	 */
	UEdGraphNode* SourceEdNode = AllCompiledEditorNodes.FindRef(SetterSource.SourceTag);
	UEdGraphNode* DestEdNode = AllCompiledEditorNodes.FindRef(DestTag);

	// Specific setter that contributed SetterSource to this group (not just any setter with the tag).
	UEdGraphNode* SetterEdNode = nullptr;
	if (const TMap<FSourcePathKey, UEdGraphNode*>* Inner = SetterEdNodeByGroupAndSource.Find(GroupTag))
	{
		SetterEdNode = Inner->FindRef(SetterSource);
	}

	// Specific getter that reaches this destination via this group (not just any getter with the tag).
	UEdGraphNode* GetterEdNode = nullptr;
	if (const TMap<FName, UEdGraphNode*>* Inner = GetterEdNodeByGroupAndDest.Find(GroupTag))
	{
		GetterEdNode = Inner->FindRef(DestTag);
	}

	// Prefer the specific path when either side has it; fall back to "any path" for the AnyPath-absorption case.
	const FString PathStr = !DirectSource.Path.IsNone()
		? DirectSource.Path.ToString()
		: (!SetterSource.Path.IsNone() ? SetterSource.Path.ToString() : TEXT("any path"));

	auto NodeTokenOrText = [this](UEdGraphNode* Node, const FString& Fallback, TSharedRef<FTokenizedMessage>& InMsg)
	{
		if (Node) AddNodeNavigationToken(InMsg, Node);
		else InMsg->AddToken(FTextToken::Create(FText::FromString(Fallback)));
	};

	TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);
	Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Parallel path: path '%s' on"), *PathStr))));
	NodeTokenOrText(SourceEdNode, SetterSource.SourceTag.ToString(), Msg);
	Msg->AddToken(FTextToken::Create(FText::FromString(TEXT("reaches"))));
	NodeTokenOrText(DestEdNode, DestTag.ToString(), Msg);
	Msg->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("both directly and via activation group '%s' (set by"), *GroupTag.ToString()))));
	NodeTokenOrText(SetterEdNode, GroupTag.ToString(), Msg);
	Msg->AddToken(FTextToken::Create(FText::FromString(TEXT(", received by"))));
	NodeTokenOrText(GetterEdNode, GroupTag.ToString(), Msg);
	Msg->AddToken(FTextToken::Create(FText::FromString(TEXT("). Consider removing one path."))));

	Messages.Add(Msg);
	NumWarnings++;

	UE_LOG(LogSimpleQuestCompiler, Warning,
		TEXT("Surface D parallel path: path '%s' on '%s' reaches '%s' both directly and via group '%s'"),
		*PathStr, *SetterSource.SourceTag.ToString(), *DestTag.ToString(), *GroupTag.ToString());
}

void FQuestlineGraphCompiler::CollectActivationGroupMetadata(UEdGraph* Graph, const FString& TagPrefix)
{
	if (!Graph) return;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		/**
		 * ActivationGroupSetter: walk backward from the Activate input to find every (source, outcome) pair that feeds this setter.
		 * CollectEffectiveSources handles knots, utility Forward, setter-Forward chains, and dereferences getters to their same-graph
		 * setters — transitive sources are captured so a parallel path through a chained group is still detected as a collision.
		 */
		if (UQuestlineNode_ActivationGroupEntry* Setter = Cast<UQuestlineNode_ActivationGroupEntry>(Node))
		{
			const FGameplayTag GroupTag = Setter->GetGroupTag();
			if (!GroupTag.IsValid()) continue;

			UEdGraphPin* ActivatePin = Setter->GetPinByRole(EQuestPinRole::ExecIn);
			if (!ActivatePin) continue;
			
			/**
			 * CollectEffectiveSources expects an output-side pin it can walk through (knots, utility Forward, setter Forward,
			 * getter Forward). Our ActivatePin is an input, so iterate its LinkedTo (each element is an output pin on an upstream
			 * node) and call the walker per-link. Sources accumulate into SourcePins across iterations via the shared out-set.
			 */
			TSet<const UEdGraphPin*> SourcePins;
			TSet<const UEdGraphNode*> VisitedNodes;
			for (const UEdGraphPin* Linked : ActivatePin->LinkedTo)
			{
				TraversalPolicy->CollectEffectiveSources(Linked, SourcePins, VisitedNodes);
			}

			for (const UEdGraphPin* SourcePin : SourcePins)
			{
				if (!SourcePin) continue;
				const UQuestlineNode_ContentBase* SourceContent = Cast<UQuestlineNode_ContentBase>(SourcePin->GetOwningNode());
				if (!SourceContent) continue; // Entry/utility sources have no content-node identity — skip

				const FString Label = Cast<const UQuestlineNode_LinkedQuestline>(SourceContent)
					? SanitizeTagSegment(SourceContent->NodeLabel.ToString())
					: SanitizeTagSegment(SourceContent->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				const FName SourceTag = MakeNodeTagName(TagPrefix, Label);
				if (SourceTag.IsNone()) continue;

				/**
				 * Outcome extraction: QuestOutcome pins carry a specific tag; QuestActivation "Any Outcome" leaves the outcome
				 * invalid to encode "any outcome from this source" — the collision test absorbs specific keys from the same source.
				 */
				FGameplayTag OutcomeTag;
				if (SourcePin->PinType.PinCategory == TEXT("QuestOutcome"))
				{
					OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(SourcePin->PinName, false);
					if (!OutcomeTag.IsValid()) continue;
				}

				const FSourcePathKey Key{ SourceTag, OutcomeTag.GetTagName() };
				GroupSetterSourcesByTag.FindOrAdd(GroupTag).Add(Key);
				SetterEdNodeByGroupAndSource.FindOrAdd(GroupTag).Add(Key, Setter);
			}
		}

		/**
		 * ActivationGroupGetter: walk forward from the Forward output to find every destination it reaches. CollectActivationTerminals
		 * terminates at content/exit Activate or Deactivate pins, passing transparently through knots, utility Forward, and
		 * setter-Forward chains. Destinations are recorded under the group tag so the analysis pass can cross-reference with
		 * setter sources for the same tag.
		 */
		if (UQuestlineNode_ActivationGroupExit* Getter = Cast<UQuestlineNode_ActivationGroupExit>(Node))
		{
			const FGameplayTag GroupTag = Getter->GetGroupTag();
			if (!GroupTag.IsValid()) continue;

			UEdGraphPin* ForwardPin = Getter->GetPinByRole(EQuestPinRole::ExecForwardOut);
			if (!ForwardPin) continue;

			TSet<const UEdGraphPin*> Terminals;
			TSet<const UEdGraphNode*> VisitedNodes;
			TraversalPolicy->CollectActivationTerminals(ForwardPin, Terminals, VisitedNodes);

			for (const UEdGraphPin* Terminal : Terminals)
			{
				if (!Terminal) continue;
				const UQuestlineNode_ContentBase* DestContent = Cast<UQuestlineNode_ContentBase>(Terminal->GetOwningNode());
				if (!DestContent) continue;

				const FString Label = Cast<const UQuestlineNode_LinkedQuestline>(DestContent)
					? SanitizeTagSegment(DestContent->NodeLabel.ToString())
					: SanitizeTagSegment(DestContent->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				const FName DestTag = MakeNodeTagName(TagPrefix, Label);
				if (DestTag.IsNone()) continue;

				GroupGetterDestsByTag.FindOrAdd(GroupTag).Add(DestTag);
				GetterEdNodeByGroupAndDest.FindOrAdd(GroupTag).Add(DestTag, Getter);			}
		}
	}
}

bool FQuestlineGraphCompiler::ResolveResettable(EResettableReplay Flag, bool bIncoming)
{
	return Flag == EResettableReplay::Enabled ? true : (Flag == EResettableReplay::Disabled ? false : bIncoming);
}

bool FQuestlineGraphCompiler::IsNodeTagResettable(FName CanonicalNodeTag) const
{
	const UQuestNodeBase* Owner = AllCompiledNodes.FindRef(CanonicalNodeTag);
	return Owner && Owner->IsResettableReplay();
}

// -------------------------------------------------------------------------------------------------
// ComputeContainerReachability — post-compile pass populating containment + reachability data
// -------------------------------------------------------------------------------------------------

void FQuestlineGraphCompiler::ComputeContainerReachability(UQuestlineGraph* InGraph)
{
    if (!InGraph) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(FQuestlineGraphCompiler_ComputeContainerReachability);

	// Resolve each compiled node's runtime ContextualTag field from its compiled FName key, plus any asset-scoped
	// alias FNames the compiler stamped during CompileNodeRegistration. ContextualTag and aliases are runtime
	// FGameplayTags normally populated by UQuestManagerSubsystem::ActivateQuestlineGraph at PIE start; at compile
	// time they're default-empty, which would silently break Steps 2 and 3 below (the InnerStepTags back-fill and
	// the routing walk both compare against ContextualTag). Both resolvers are idempotent — runtime calls them
	// again at graph load with no side effects beyond re-assignment. Skip Util_ keys — utility nodes use a
	// GUID-derived FName that isn't a registered gameplay tag.
	for (const auto& Pair : InGraph->CompiledNodes)
	{
		if (Pair.Value && !Pair.Key.ToString().StartsWith(TEXT("Util_")))
		{
			Pair.Value->ResolveContextualTag(Pair.Key);
			if (const TArray<FName>* AliasFNames = CompiledAliasFNamesByContextualTag.Find(Pair.Key))
			{
				Pair.Value->ResolveAssetScopedAliasTags(*AliasFNames);
			}
		}
	}
	
    // ---- Step 1: Per-Step ancestor chain (innermost-first) via ImmediateContainerByTag walk ----
    for (const auto& Pair : InGraph->CompiledNodes)
    {
        UQuestStep* Step = Cast<UQuestStep>(Pair.Value);
        if (!Step) continue;
        Step->AncestorContainerTags.Reset();
        FName Cursor = ImmediateContainerByTag.FindRef(Pair.Key);
        while (Cursor != NAME_None)
        {
            const FGameplayTag CursorTag = FGameplayTag::RequestGameplayTag(Cursor, /*bErrorIfNotFound=*/false);
            if (CursorTag.IsValid())
            {
                Step->AncestorContainerTags.Add(CursorTag);
            }
            Cursor = ImmediateContainerByTag.FindRef(Cursor);
        }
    }

    // ---- Step 2: Per-container InnerStepTags (any-depth Steps inside) ----
    // Iterate Steps and back-fill each ancestor's InnerStepTags. Result is: a Step's ContextualTag appears in the
    // InnerStepTags of every container in its ancestor chain.
    for (const auto& Pair : InGraph->CompiledNodes)
    {
        UQuestStep* Step = Cast<UQuestStep>(Pair.Value);
        if (!Step || !Step->ContextualTag.IsValid()) continue;
        for (const FGameplayTag& AncestorTag : Step->AncestorContainerTags)
        {
            UQuestNodeBase* AncestorInstance = InGraph->CompiledNodes.FindRef(AncestorTag.GetTagName()).Get();
            if (UQuest* AncestorQuest = Cast<UQuest>(AncestorInstance))
            {
                AncestorQuest->InnerStepTags.AddUnique(Step->ContextualTag);
            }
        }
    }

    // ---- Step 3: Per-container ReachableStepsByActivatePin via routing walk filtered by containment ----
    auto IsInsideContainer = [this](FName CandidateTag, FName ContainerTagName) -> bool
    {
        FName Cursor = ImmediateContainerByTag.FindRef(CandidateTag);
        while (Cursor != NAME_None)
        {
            if (Cursor == ContainerTagName) return true;
            Cursor = ImmediateContainerByTag.FindRef(Cursor);
        }
        return false;
    };

    // Activation-immediate reachability — follow ONLY entry routes (entry pins of nested wrappers; utility
    // node forward outputs). Skip completion outputs (NextNodesByPath / NextNodesOnAnyOutcome on Steps and
    // wrappers): those represent "Step finishes, then next thing activates," not "this pin's give-acceptance
    // immediately activates that Step." Phase 6's giver gate skip should fire only when all Steps that this
    // pin's activation IMMEDIATELY enables are already Live; downstream outcome-chain Steps don't count.
    TFunction<TArray<FGameplayTag>(UQuest*, const TArray<FName>&)> WalkEntryReachable;
    WalkEntryReachable = [&](UQuest* Container, const TArray<FName>& EntryDestinations) -> TArray<FGameplayTag>
    {
        TSet<FName> Visited;
        TArray<FName> Frontier = EntryDestinations;
        TArray<FGameplayTag> Reached;
        const FName ContainerTagName = Container->ContextualTag.GetTagName();

        while (Frontier.Num() > 0)
        {
            const FName Current = Frontier.Pop(EAllowShrinking::No);
            if (Visited.Contains(Current)) continue;
            Visited.Add(Current);

            UQuestNodeBase* Node = InGraph->CompiledNodes.FindRef(Current).Get();
            if (!Node) continue;

            if (UQuestStep* Step = Cast<UQuestStep>(Node))
            {
                if (Step->ContextualTag.IsValid()) Reached.AddUnique(Step->ContextualTag);
                // Don't follow Step's outcomes — those are completion routes.
            }
            else if (UQuest* InnerWrapper = Cast<UQuest>(Node))
            {
                // Recurse into the nested wrapper's any-outcome entry routes. Outer's entry to inner via
                // wrapper.Activate corresponds to inner's any-outcome entry pin (inner.EntryStepTags). Don't
                // absorb inner.InnerStepTags or follow inner's outer outcomes — those include outcome-chain
                // Steps that activate later, not immediately on this pin's give-acceptance.
                const TArray<FGameplayTag> InnerReached = WalkEntryReachable(InnerWrapper, InnerWrapper->GetEntryStepTags());
                for (const FGameplayTag& T : InnerReached) Reached.AddUnique(T);
            }
            else
            {
                // Utility / control node — follow NextNodesOnForward (activation-forward wire, not completion).
                for (FName Dest : Node->GetNextNodesOnForward())
                {
                    if (IsInsideContainer(Dest, ContainerTagName)) Frontier.Add(Dest);
                }
            }
        }
        return Reached;
    };

    for (const auto& Pair : InGraph->CompiledNodes)
    {
        UQuest* Container = Cast<UQuest>(Pair.Value);
        if (!Container) continue;

        Container->ReachableStepsByActivatePin.Reset();

        // Any-Outcome entry pin: stored under NAME_None.
        FQuestReachableSteps AnySteps;
        AnySteps.StepTags = WalkEntryReachable(Container, Container->GetEntryStepTags());
        Container->ReachableStepsByActivatePin.Add(NAME_None, MoveTemp(AnySteps));

        // Per-path entry pins — keys mirror EntryStepTagsByPath.
        for (const auto& EntryPair : Container->GetEntryStepTagsByPath())
        {
            TArray<FName> PathDests;
            PathDests.Reserve(EntryPair.Value.Destinations.Num());
            for (const FQuestEntryDestination& Dest : EntryPair.Value.Destinations)
            {
                PathDests.Add(Dest.DestTag);
            }
            FQuestReachableSteps PathSteps;
            PathSteps.StepTags = WalkEntryReachable(Container, PathDests);
            Container->ReachableStepsByActivatePin.Add(EntryPair.Key, MoveTemp(PathSteps));
        }
    }

    // ---- Step 4: Verbose log dump for verification ----
    UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("ComputeContainerReachability: graph '%s' — %d compiled node(s)"),
        *InGraph->GetName(), InGraph->CompiledNodes.Num());

    for (const auto& Pair : InGraph->CompiledNodes)
    {
        if (UQuest* Container = Cast<UQuest>(Pair.Value))
        {
            UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("  Container '%s' — %d inner Step(s), %d Activate-pin entry(s)"),
                *Pair.Key.ToString(), Container->InnerStepTags.Num(), Container->ReachableStepsByActivatePin.Num());
            for (const FGameplayTag& T : Container->InnerStepTags)
            {
                UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("    InnerStep: %s"), *T.GetTagName().ToString());
            }
            for (const auto& PinPair : Container->ReachableStepsByActivatePin)
            {
                const FString PinLabel = (PinPair.Key == NAME_None) ? TEXT("AnyOutcome") : PinPair.Key.ToString();
                UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("    ActivatePin '%s' → %d Step(s)"),
                    *PinLabel, PinPair.Value.StepTags.Num());
                for (const FGameplayTag& T : PinPair.Value.StepTags)
                {
                    UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("      reachable Step: %s"), *T.GetTagName().ToString());
                }
            }
        }
        else if (UQuestStep* Step = Cast<UQuestStep>(Pair.Value))
        {
            FString AncestorList;
            for (const FGameplayTag& T : Step->AncestorContainerTags)
            {
                if (!AncestorList.IsEmpty()) AncestorList += TEXT(" → ");
                AncestorList += T.GetTagName().ToString();
            }
            if (AncestorList.IsEmpty()) AncestorList = TEXT("(root)");
            UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("  Step '%s' — Ancestors=[%s]"),
                *Pair.Key.ToString(), *AncestorList);
        }
    }
}

void FQuestlineGraphCompiler::BuildRewardManifest(UQuestlineGraph* InGraph)
{
	if (!InGraph) return;

	// Collect reward-node keys reachable from a seed set of route destinations. Reward nodes are collected AND
	// traversed (chained rewards downstream of a reward still count); other utils pass through; content nodes stop.
	auto WalkRewards = [InGraph](const TArray<FName>& Seed) -> TArray<FName>
	{
		TArray<FName> Result;
		TArray<FName> Frontier = Seed;
		TSet<FName>   Visited;

		while (Frontier.Num() > 0)
		{
			const FName Key = Frontier.Pop(EAllowShrinking::No);
			if (Visited.Contains(Key)) continue;
			Visited.Add(Key);

			UQuestNodeBase* Node = InGraph->CompiledNodes.FindRef(Key).Get();
			if (!Node) continue;

			if (Cast<UQuestRewardNode>(Node))
			{
				Result.AddUnique(Key);
				Frontier.Append(Node->GetNextNodesOnForward().Array());		// chained rewards + trailing utils
			}
			else if (Cast<UQuestStep>(Node) || Cast<UQuest>(Node))
			{
				// Next lifecycle unit — its rewards are its own. Stop here.
			}
			else
			{
				Frontier.Append(Node->GetNextNodesOnForward().Array());		// pass-through util (Add Fact, gate, ...)
			}
		}
		return Result;
	};

	for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : InGraph->CompiledNodes)
	{
		UQuestNodeBase* Node = Pair.Value.Get();
		if (!Node || !(Cast<UQuestStep>(Node) || Cast<UQuest>(Node))) continue;		// only completing nodes advertise

		Node->ReachableRewardsByPath.Reset();

		// Map each Step path to its registered outcome tag (invalid for dynamic paths). Containers always route on
		// registered outcome tags, so their path key IS the tag name — no descriptor lookup needed.
		TMap<FName, FGameplayTag> OutcomeByPath;
		if (const UQuestStep* Step = Cast<UQuestStep>(Node))
		{
			for (const FObjectivePathDescriptor& Desc : FSimpleQuestEditorUtilities::DiscoverObjectivePaths(Step->GetQuestObjective().LoadSynchronous()))
			{
				OutcomeByPath.Add(Desc.Identity, Desc.Outcome);
			}
		}
		const bool bIsStepNode = Cast<UQuestStep>(Node) != nullptr;

		auto LabelForPath = [&](FName PathId) -> FText
		{
			if (PathId.IsNone()) return NSLOCTEXT("SimpleQuest", "RewardPathOnCompletion", "On completion");
			// The outcome tag: for a Step, from the descriptor; for a container, the path key is itself a registered tag.
			const FGameplayTag Outcome = bIsStepNode ? OutcomeByPath.FindRef(PathId)
													 : UGameplayTagsManager::Get().RequestGameplayTag(PathId, false);
			// Culture-invariant, NOT FromString. A keyless FText gets a fresh localization key minted for it every time
			// the package saves, so the next compile regenerates the keyless version, the two compare unequal, and the
			// asset dirties again with no authored change behind it. These labels are gameplay-tag leaf names rather
			// than authored prose - there is nothing to translate - so marking them invariant is both the fix and an
			// accurate description of what they are. The "On completion" literal above keeps its NSLOCTEXT key and is
			// stable precisely because it has one.
			if (!Outcome.IsValid()) return FText::AsCultureInvariant(PathId.ToString());	// dynamic PathName - show as authored
			const FString Full = Outcome.ToString();										// registered tag - show the leaf
			int32 Dot; return FText::AsCultureInvariant(Full.FindLastChar(TEXT('.'), Dot) ? Full.RightChop(Dot + 1) : Full);
		};

		// Any-outcome bucket (NAME_None).
		if (TArray<FName> AnyRewards = WalkRewards(Node->GetNextNodesOnAnyOutcome().Array()); AnyRewards.Num() > 0)
		{
			Node->ReachableRewardsByPath.Add(NAME_None, { LabelForPath(NAME_None), MoveTemp(AnyRewards) });
		}

		// Per-path buckets — keys mirror NextNodesByPath.
		for (const TPair<FName, FQuestPathNodeList>& PathPair : Node->GetNextNodesByPath())
		{
			if (TArray<FName> PathRewards = WalkRewards(PathPair.Value.NodeTags); PathRewards.Num() > 0)
			{
				Node->ReachableRewardsByPath.Add(PathPair.Key, { LabelForPath(PathPair.Key), MoveTemp(PathRewards) });
			}
		}
		
		UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("BuildRewardManifest: '%s' advertises rewards on %d path(s)"),
			*Node->GetName(),
			Node->ReachableRewardsByPath.Num());
	}
}

