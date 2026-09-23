// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Debug/QuestPIEDebugChannel.h"

#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestManagerSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Types/PrereqExaminerTypes.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestTagComposer.h"
#include "Utilities/SimpleQuestEditorUtils.h"


namespace
{
	bool HasAnyStateFact(const FGameplayTag& NodeTag, UWorldStateSubsystem* WorldState)
	{
		for (EQuestStateLeaf Leaf : FQuestTagComposer::AllStateLeaves)
		{
			const FGameplayTag Fact = FQuestTagComposer::ResolveStateFactTag(NodeTag, Leaf);
			if (Fact.IsValid() && WorldState->HasFact(Fact)) return true;
		}
		return false;
	}
}

void FQuestPIEDebugChannel::Initialize()
{
	PostPIEStartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(this, &FQuestPIEDebugChannel::HandlePostPIEStarted);
	EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FQuestPIEDebugChannel::HandleEndPIE);
	UE_LOG(LogSimpleQuest, Verbose, TEXT("FQuestPIEDebugChannel::Initialize : subscribed to PostPIEStarted/EndPIE"));
}

void FQuestPIEDebugChannel::Shutdown()
{
	if (PostPIEStartedHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(PostPIEStartedHandle);
		PostPIEStartedHandle.Reset();
	}
	if (EndPIEHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		EndPIEHandle.Reset();
	}
	if (UQuestStateSubsystem* QS = CachedQuestState.Get())
	{
		if (OnAnyRegistryChangedHandle.IsValid())
		{
			QS->OnAnyRegistryChanged.Remove(OnAnyRegistryChangedHandle);
		}
	}
	OnAnyRegistryChangedHandle.Reset();
	CachedWorldState.Reset();
	CachedQuestManager.Reset();
	CachedQuestState.Reset();
	SessionHistory.Reset();
	NextSessionNumber = 1;
	bIsActive = false;
}

bool FQuestPIEDebugChannel::IsActive() const
{
	return bIsActive && CachedWorldState.IsValid() && CachedQuestManager.IsValid();
}

void FQuestPIEDebugChannel::HandlePostPIEStarted(bool bIsSimulating)
{
	const bool bResolved = ResolvePIESubsystems();
	bIsActive = bResolved;
	UE_LOG(LogSimpleQuest, Display, TEXT("FQuestPIEDebugChannel : PIE started (simulating=%d, subsystems resolved=%d)"),
		bIsSimulating ? 1 : 0, bResolved ? 1 : 0);

	// Always begin a new session even if QuestState didn't resolve - the snapshot fields stay empty in that case
	// (graceful degradation). This matches IsActive()'s policy: WorldState + QuestManager are load-bearing, QuestState
	// is optional. Subscribe to OnAnyRegistryChanged only if the subsystem resolved.
	if (bResolved)
	{
		BeginNewSession();
		if (UQuestStateSubsystem* QS = CachedQuestState.Get())
		{
			OnAnyRegistryChangedHandle = QS->OnAnyRegistryChanged.AddRaw(this, &FQuestPIEDebugChannel::HandleAnyRegistryChanged);
		}
	}
	OnDebugActiveChanged.Broadcast();
}

void FQuestPIEDebugChannel::HandleEndPIE(bool bIsSimulating)
{
	// Finalize before resetting CachedQuestState - finalize reads the live subsystem to capture registry maps.
	FinalizeInFlightSession();

	if (UQuestStateSubsystem* QS = CachedQuestState.Get())
	{
		if (OnAnyRegistryChangedHandle.IsValid())
		{
			QS->OnAnyRegistryChanged.Remove(OnAnyRegistryChangedHandle);
		}
	}
	OnAnyRegistryChangedHandle.Reset();

	CachedWorldState.Reset();
	CachedQuestManager.Reset();
	CachedQuestState.Reset();

	// Placements exist only while the game runs, so a selection cannot outlive the session that produced it. Clearing here
	// also means the next session starts on the merged default rather than on a tag that named a different run's instance.
	DebugContextByAsset.Reset();

	bIsActive = false;
	UE_LOG(LogSimpleQuest, Display, TEXT("FQuestPIEDebugChannel : PIE ended"));
	OnDebugActiveChanged.Broadcast();
}

bool FQuestPIEDebugChannel::ResolvePIESubsystems()
{
	if (!GEditor)
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("FQuestPIEDebugChannel::ResolvePIESubsystems : GEditor is null"));
		return false;
	}

	// Primary path: GEditor->PlayWorld - canonical access to the PIE/SIE world while play is active. Non-null for both
	// Play In Editor and Simulate In Editor. Avoids the world-context iteration edge cases (RunAsDedicated semantics,
	// multi-instance PIE) that complicate the loop-based path.
	UWorld* PIEWorld = GEditor->PlayWorld;

	if (!PIEWorld)
	{
		// Fallback: iterate world contexts looking for any PIE-type world. Covers edge cases where PlayWorld isn't set
		// but a PIE context exists (rare - certain dedicated-server-only startup flows).
		for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
			{
				PIEWorld = Ctx.World();
				UE_LOG(LogSimpleQuest, Verbose, TEXT("FQuestPIEDebugChannel::ResolvePIESubsystems : PlayWorld null, using fallback context '%s'"),
					*PIEWorld->GetName());
				break;
			}
		}
	}

	if (!PIEWorld)
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("FQuestPIEDebugChannel::ResolvePIESubsystems : no PIE/SIE world found (GEditor->PlayWorld null; no PIE-type world context)"));
		return false;
	}

	UGameInstance* GI = PIEWorld->GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("FQuestPIEDebugChannel::ResolvePIESubsystems : world '%s' (type=%d) has no GameInstance"),
			*PIEWorld->GetName(), static_cast<int32>(PIEWorld->WorldType));
		return false;
	}

	CachedWorldState = GI->GetSubsystem<UWorldStateSubsystem>();
	CachedQuestManager = GI->GetSubsystem<UQuestManagerSubsystem>();
	CachedQuestState = GI->GetSubsystem<UQuestStateSubsystem>();

	UE_LOG(LogSimpleQuest, Display, TEXT("FQuestPIEDebugChannel::ResolvePIESubsystems : world='%s' type=%d, WorldState=%s, QuestManager=%s, QuestState=%s"),
		*PIEWorld->GetName(), static_cast<int32>(PIEWorld->WorldType),
		CachedWorldState.IsValid() ? TEXT("resolved") : TEXT("NULL"),
		CachedQuestManager.IsValid() ? TEXT("resolved") : TEXT("NULL"),
		CachedQuestState.IsValid() ? TEXT("resolved") : TEXT("NULL"));
	
	// IsActive() condition unchanged: WorldState + QuestManager are the load-bearing pair for existing queries
	// (QueryNodeState, TryGetPrereqStatusForNode, HasFact). QuestState is a separately-checked optional resource for the
	// Quest State view; failures to resolve it don't disable the rest of the channel.
	return CachedWorldState.IsValid() && CachedQuestManager.IsValid();
}

EQuestNodeDebugState FQuestPIEDebugChannel::QueryNodeState(const UEdGraphNode* EditorNode) const
{
	if (!IsActive() || !EditorNode) return EQuestNodeDebugState::Unknown;

	if (!Cast<UQuestlineNode_ContentBase>(EditorNode)) return EQuestNodeDebugState::Unknown;

	const FGameplayTag RuntimeTag = ResolveRuntimeTag(EditorNode);
	if (!RuntimeTag.IsValid()) return EQuestNodeDebugState::Unknown;

	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	if (!WorldState) return EQuestNodeDebugState::Unknown;

	// Priority order: PendingGiver > Live > Completed > Deactivated - current activity outranks history. The
	// Completed fact is append-only and never clears, and a container holds its Live fact across loop iterations, so a
	// node running again asserts both at once; ranking Completed first paints "done" over something actively running.
	// Routes through FQuestLifecycleQuery so this surface answers the same "is this state asserted?" question every
	// other site does, rather than a separate fact-resolve path that drifts as the centralized helpers evolve.
	if (FQuestLifecycleQuery::IsPendingGiver(WorldState, RuntimeTag)) return EQuestNodeDebugState::PendingGiver;
	if (FQuestLifecycleQuery::IsLive(WorldState, RuntimeTag))         return EQuestNodeDebugState::Live;
	if (FQuestLifecycleQuery::IsCompleted(WorldState, RuntimeTag))    return EQuestNodeDebugState::Completed;
	if (FQuestLifecycleQuery::IsDeactivated(WorldState, RuntimeTag))  return EQuestNodeDebugState::Deactivated;

	return EQuestNodeDebugState::Unknown;
}

TArray<FQuestActivationBlocker> FQuestPIEDebugChannel::QueryNodeGating(const UEdGraphNode* EditorNode) const
{
	TArray<FQuestActivationBlocker> Out;
	if (!IsActive()) return Out;

	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	if (!WorldState) return Out;

	const FGameplayTag RuntimeTag = ResolveRuntimeTag(EditorNode);
	if (RuntimeTag.IsValid() && FQuestLifecycleQuery::IsBlocked(WorldState, RuntimeTag))
	{
		Out.AddDefaulted_GetRef().Reason = EQuestActivationBlocker::Blocked;
	}

	// Asked of the MANAGER rather than read as a fact, and the distinction matters. The Held fact is written on the
	// tag the hold NAMES; a node paused by a hold placed on a container above it carries no fact of its own. Only the
	// manager walks ancestry across every tag perspective, which is the question this overlay actually needs answered.
	if (RuntimeTag.IsValid())
	{
		if (const UQuestManagerSubsystem* Manager = CachedQuestManager.Get())
		{
			if (Manager->IsQuestAdvancementHeld(RuntimeTag))
			{
				Out.AddDefaulted_GetRef().Reason = EQuestActivationBlocker::HeldForAdvancement;
			}
		}
	}

	// Prereq state is evaluated live rather than read from the state subsystem's blocker query, which sources its
	// PrereqUnmet case from a cache populated only for giver-gated quests. Evaluating also fills the unsatisfied-leaf
	// list accurately for nodes that query would have no entry for at all.
	FQuestPrereqStatus Status;
	if (TryGetPrereqStatusForNode(EditorNode, Status) && !Status.bIsAlways && !Status.bSatisfied)
	{
		FQuestActivationBlocker& Blocker = Out.AddDefaulted_GetRef();
		Blocker.Reason = EQuestActivationBlocker::PrereqUnmet;
		for (const FQuestPrereqLeafStatus& Leaf : Status.Leaves)
		{
			if (!Leaf.bSatisfied) Blocker.UnsatisfiedLeafTags.Add(Leaf.LeafTag);
		}
	}
	return Out;
}

float FQuestPIEDebugChannel::GetRefusalPulseAlpha(const UEdGraphNode* EditorNode) const
{
	if (!IsActive()) return 0.f;

	UQuestStateSubsystem* StateSubsystem = CachedQuestState.Get();
	if (!StateSubsystem) return 0.f;

	const FGameplayTag RuntimeTag = ResolveRuntimeTag(EditorNode);
	if (!RuntimeTag.IsValid()) return 0.f;

	// The history is written across perspectives, so whichever spelling ResolveRuntimeTag produced will find it.
	const TArray<FQuestRefusalEntry> History = StateSubsystem->GetRefusalHistory(RuntimeTag);
	if (History.Num() == 0) return 0.f;

	const double Elapsed = GetCurrentGameTimeSeconds() - History.Last().RefusalTime;
	if (Elapsed < 0.0 || Elapsed > RefusalPulseSeconds) return 0.f;
	return 1.f - static_cast<float>(Elapsed / RefusalPulseSeconds);
}

bool FQuestPIEDebugChannel::TryGetPrereqStatusForNode(const UEdGraphNode* EditorNode, FQuestPrereqStatus& OutStatus) const
{
	if (!IsActive() || !EditorNode) return false;

	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	UQuestStateSubsystem* StateSubsystem = CachedQuestState.Get();
	UQuestManagerSubsystem* Manager = CachedQuestManager.Get();
	if (!WorldState || !StateSubsystem || !Manager)
	{
		return false;
	}

	const UQuestNodeBase* Instance = nullptr;
	const FGameplayTag RuntimeTag = ResolveRuntimeTag(EditorNode);
	if (RuntimeTag.IsValid())
	{
		Instance = Manager->FindNodeInstance(RuntimeTag);
		if (!Instance)
		{
			// The opened asset can be a standalone questline whose placement runs under a parent's namespace. Facts publish on
			// every perspective, so ResolveRuntimeTag's own contextual swap does not fire here - but the instance registry is
			// keyed by the running placement's contextual tag alone, so resolve through the runtime alias index instead.
			for (const FGameplayTag& Canonical : StateSubsystem->ResolveCanonicalTags(RuntimeTag))
			{
				if (Canonical == RuntimeTag) continue;
				Instance = Manager->FindNodeInstance(Canonical);
				if (Instance)
				{
					break;
				}
			}
		}
	}
	else if (const UQuestlineNodeBase* TaglessNode = Cast<UQuestlineNodeBase>(EditorNode))
	{
		// A utility node with a Prerequisites pin - the Prerequisite Gate - compiles to an instance that carries an
		// expression but no tag, so the tag-keyed lookups above cannot reach it. Its authored guid is stable across every
		// compile context the node appears in, and that is the handle the manager keeps for it.
		Instance = Manager->FindNodeInstanceByAuthoredGuid(TaglessNode->QuestGuid);
	}
	if (!Instance) return false;

	OutStatus = Instance->GetPrerequisiteStatus(WorldState, StateSubsystem);
	return true;
}

EPrereqDebugState FQuestPIEDebugChannel::QueryLeafStateForSource(const UEdGraphNode* OwnerNode, FGameplayTag SourceTag, FName PathIdentity, bool bAnyOutcome) const
{
	FQuestPrereqStatus Status;
	if (!TryGetPrereqStatusForNode(OwnerNode, Status) || Status.bIsAlways) return EPrereqDebugState::Unknown;
	if (!SourceTag.IsValid()) return EPrereqDebugState::Unknown;

	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	UQuestStateSubsystem* StateSubsystem = CachedQuestState.Get();
	if (!WorldState || !StateSubsystem) return EPrereqDebugState::Unknown;

	// The opened asset and the running placement spell the same node differently, so build the set of spellings once
	// and compare against all of them rather than trusting the one the editor happens to hold.
	TArray<FGameplayTag> SourceSpellings = StateSubsystem->ResolveCanonicalTags(SourceTag);
	SourceSpellings.AddUnique(SourceTag);

	bool bMatchedAny = false;
	bool bAnySatisfied = false;
	for (const FQuestPrereqLeafStatus& Leaf : Status.Leaves)
	{
		if (!Leaf.SourceQuestTag.IsValid() || !SourceSpellings.Contains(Leaf.SourceQuestTag)) continue;
		if (!bAnyOutcome && Leaf.SourcePathIdentity != PathIdentity) continue;

		bMatchedAny = true;
		bAnySatisfied |= Leaf.bSatisfied;
		if (!bAnyOutcome) break;
	}

	if (!bMatchedAny) return EPrereqDebugState::Unknown;
	if (bAnySatisfied) return EPrereqDebugState::Satisfied;

	// Not satisfied. Which of the three remaining states applies is the SOURCE node's own lifecycle, read under every
	// spelling the running instance answers to: still running (or waiting on a giver) means the outcome is undecided;
	// any other state fact means it ran and did not produce this path; no facts at all means it has not been reached.
	for (const FGameplayTag& Spelling : SourceSpellings)
	{
		if (FQuestLifecycleQuery::IsLive(WorldState, Spelling) || FQuestLifecycleQuery::IsPendingGiver(WorldState, Spelling))
		{
			return EPrereqDebugState::InProgress;
		}
	}
	for (const FGameplayTag& Spelling : SourceSpellings)
	{
		if (HasAnyStateFact(Spelling, WorldState)) return EPrereqDebugState::Unsatisfied;
	}
	return EPrereqDebugState::NotStarted;
}

EPrereqDebugState FQuestPIEDebugChannel::QueryLeafStateForFact(const UEdGraphNode* OwnerNode, FGameplayTag LeafTag) const
{
	FQuestPrereqStatus Status;
	if (!TryGetPrereqStatusForNode(OwnerNode, Status) || Status.bIsAlways) return EPrereqDebugState::Unknown;
	if (!LeafTag.IsValid()) return EPrereqDebugState::Unknown;

	// Fact and context-free outcome leaves carry the tag they read as LeafTag and no source quest; path, resolution, and
	// entry leaves carry a source and correlate through QueryLeafStateForSource instead. A raw fact has no lifecycle of
	// its own to refine "unsatisfied" with, so these two are the whole range.
	for (const FQuestPrereqLeafStatus& Leaf : Status.Leaves)
	{
		if (Leaf.SourceQuestTag.IsValid() || Leaf.LeafTag != LeafTag) continue;
		return Leaf.bSatisfied ? EPrereqDebugState::Satisfied : EPrereqDebugState::Unsatisfied;
	}
	return EPrereqDebugState::Unknown;
}

EPrereqDebugState FQuestPIEDebugChannel::EvaluateExaminerNode(const FPrereqExaminerTree& Tree, int32 NodeIndex) const
{
	if (!IsActive() || !Tree.Nodes.IsValidIndex(NodeIndex)) return EPrereqDebugState::Unknown;

	const FPrereqExaminerNode& Node = Tree.Nodes[NodeIndex];
	switch (Node.Type)
	{
	case EPrereqExaminerNodeType::Leaf:
	{
		// Evaluate against the node whose COMPILED expression holds this leaf - the pinned node when it is a content
		// node or gate, otherwise the consumer the builder found downstream of a combinator or rule. Content-sourced
		// leaves correlate on source node plus completion path, because Any Outcome expands to one compiled leaf per
		// path and the channel ORs them; fact and outcome leaves have no source and correlate on the tag they read.
		const UEdGraphNode* Owner = Tree.EvaluationNode.Get();
		if (Node.LeafSourceTag.IsValid())
		{
			return QueryLeafStateForSource(Owner, Node.LeafSourceTag, Node.LeafPathIdentity, Node.bLeafIsAnyOutcome);
		}
		if (Node.LeafTag.IsValid())
		{
			return QueryLeafStateForFact(Owner, Node.LeafTag);
		}
		return EPrereqDebugState::Unknown;
	}
	case EPrereqExaminerNodeType::And:
	{
		bool bAllSatisfied = true;
		for (int32 ChildIdx : Node.ChildIndices)
		{
			const EPrereqDebugState ChildState = EvaluateExaminerNode(Tree, ChildIdx);
			if (ChildState == EPrereqDebugState::Unknown) return EPrereqDebugState::Unknown;
			if (ChildState != EPrereqDebugState::Satisfied) bAllSatisfied = false;
		}
		return (Node.ChildIndices.Num() > 0 && bAllSatisfied) ? EPrereqDebugState::Satisfied : EPrereqDebugState::Unsatisfied;
	}
	case EPrereqExaminerNodeType::Or:
	{
		for (int32 ChildIdx : Node.ChildIndices)
		{
			const EPrereqDebugState ChildState = EvaluateExaminerNode(Tree, ChildIdx);
			if (ChildState == EPrereqDebugState::Unknown) return EPrereqDebugState::Unknown;
			if (ChildState == EPrereqDebugState::Satisfied) return EPrereqDebugState::Satisfied;
		}
		return EPrereqDebugState::Unsatisfied;
	}
	case EPrereqExaminerNodeType::Not:
	{
		if (Node.ChildIndices.Num() == 0) return EPrereqDebugState::Unknown;
		const EPrereqDebugState ChildState = EvaluateExaminerNode(Tree, Node.ChildIndices[0]);
		if (ChildState == EPrereqDebugState::Unknown) return EPrereqDebugState::Unknown;
		return (ChildState == EPrereqDebugState::Satisfied) ? EPrereqDebugState::Unsatisfied : EPrereqDebugState::Satisfied;
	}
	case EPrereqExaminerNodeType::RuleRef:
	{
		if (Node.ChildIndices.Num() == 0) return EPrereqDebugState::Unknown;
		return EvaluateExaminerNode(Tree, Node.ChildIndices[0]);
	}
	default:
		return EPrereqDebugState::Unknown;
	}
}

const UQuestlineGraph* FQuestPIEDebugChannel::FindOwningAsset(const UEdGraphNode* EditorNode)
{
	if (!EditorNode) return nullptr;

	UObject* Outer = EditorNode->GetGraph() ? EditorNode->GetGraph()->GetOuter() : nullptr;
	while (Outer && !Outer->IsA<UQuestlineGraph>()) Outer = Outer->GetOuter();
	return Cast<UQuestlineGraph>(Outer);
}

FGameplayTag FQuestPIEDebugChannel::NarrowToDebugContext(const UEdGraphNode* EditorNode, const TArray<FGameplayTag>& Candidates) const
{
	const UQuestlineGraph* OwningAsset = FindOwningAsset(EditorNode);
	if (!OwningAsset) return FGameplayTag();

	const FGameplayTag* Context = DebugContextByAsset.Find(OwningAsset);
	if (!Context || !Context->IsValid()) return FGameplayTag();

	// MatchesTag is "this tag is the argument or a descendant of it", so one comparison covers a placement at any depth: a
	// route placed in a chapter that is itself placed in the master reads the same as a placement sitting at the top.
	for (const FGameplayTag& Candidate : Candidates)
	{
		if (Candidate.MatchesTag(*Context)) return Candidate;
	}
	return FGameplayTag();
}

void FQuestPIEDebugChannel::SetDebugContextForAsset(const UQuestlineGraph* OpenedAsset, FGameplayTag PlacementTag)
{
	if (!OpenedAsset) return;

	if (PlacementTag.IsValid())
	{
		DebugContextByAsset.Add(OpenedAsset, PlacementTag);
	}
	else
	{
		DebugContextByAsset.Remove(OpenedAsset);
	}

	UE_LOG(LogSimpleQuest, Verbose, TEXT("FQuestPIEDebugChannel : debug context for '%s' is now %s"),
		*OpenedAsset->GetName(),
		PlacementTag.IsValid() ? *PlacementTag.ToString() : TEXT("all placements"));

	OnDebugActiveChanged.Broadcast();
}

FGameplayTag FQuestPIEDebugChannel::GetDebugContextForAsset(const UQuestlineGraph* OpenedAsset) const
{
	if (!OpenedAsset) return FGameplayTag();
	const FGameplayTag* Context = DebugContextByAsset.Find(OpenedAsset);
	return Context ? *Context : FGameplayTag();
}

TArray<FGameplayTag> FQuestPIEDebugChannel::GetPlacementsForAsset(const UQuestlineGraph* OpenedAsset) const
{
	if (!IsActive() || !OpenedAsset || OpenedAsset->GetCompiledIdentityTag().IsNone()) return {};

	const UQuestManagerSubsystem* Manager = CachedQuestManager.Get();
	if (!Manager) return {};

	const FGameplayTag AssetIdentity = FGameplayTag::RequestGameplayTag(OpenedAsset->GetCompiledIdentityTag(), false);
	if (!AssetIdentity.IsValid()) return {};

	return Manager->FindPlacementsOfAsset(AssetIdentity);
}

FGameplayTag FQuestPIEDebugChannel::ResolveRuntimeTag(const UEdGraphNode* EditorNode) const
{
	if (!EditorNode) return FGameplayTag();

	const UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(EditorNode);
	if (!ContentNode) return FGameplayTag();

	// Resolve own-asset compile tag via Outer walk + CompiledNodes lookup (same as before).
	const UQuestlineGraph* QuestlineAsset = FindOwningAsset(EditorNode);
	if (!QuestlineAsset) return FGameplayTag();

	FGameplayTag OwnAssetTag;
	for (const auto& [TagName, NodeInstance] : QuestlineAsset->GetCompiledNodes())
	{
		if (NodeInstance && NodeInstance->GetQuestGuid() == ContentNode->QuestGuid)
		{
			OwnAssetTag = FGameplayTag::RequestGameplayTag(TagName, false);
			break;
		}
	}

	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	UQuestStateSubsystem* StateSubsystem = CachedQuestState.Get();
	if (!WorldState || !StateSubsystem || !OwnAssetTag.IsValid()) return OwnAssetTag;

	// A placement selected in the toolbar names one instance, and it wins outright. Without this the branch below returns the
	// asset's own tag whenever that tag carries state - which it always does for a placed asset, because facts are written at
	// every perspective - and an asset placed twice reads as its placements merged.
	if (const FGameplayTag InContext = NarrowToDebugContext(EditorNode, StateSubsystem->ResolveCanonicalTags(OwnAssetTag));
		InContext.IsValid())
	{
		return InContext;
	}

	// Contextual resolution: if PIE is running and own-asset tag has no live state, the asset may be opened
	// while a parent LinkedQuestline placement is the actively running instance. Consult the state subsystem's
	// runtime alias index (ResolveCanonicalTags) instead of the editor-utility's asset-registry walk - the
	// runtime index reflects post-game-start registrations (including listener auto-load), whereas the asset-
	// registry walk only sees compile-time data and can disagree with what the manager has actually registered.
	// Closes the "halo doesn't update post-listener" symptom.
	if (!HasAnyStateFact(OwnAssetTag, WorldState))
	{
		for (const FGameplayTag& CanonicalTag : StateSubsystem->ResolveCanonicalTags(OwnAssetTag))
		{
			if (CanonicalTag != OwnAssetTag && HasAnyStateFact(CanonicalTag, WorldState))
			{
				return CanonicalTag;
			}
		}
	}
	return OwnAssetTag;
}

bool FQuestPIEDebugChannel::HasFact(const FGameplayTag& FactTag) const
{
	if (!IsActive() || !FactTag.IsValid()) return false;
	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	return WorldState && WorldState->HasFact(FactTag);
}

UQuestStateSubsystem* FQuestPIEDebugChannel::GetQuestStateSubsystem() const
{
	return CachedQuestState.Get();
}

double FQuestPIEDebugChannel::GetCurrentGameTimeSeconds() const
{
	const UQuestManagerSubsystem* QM = CachedQuestManager.Get();
	if (!QM) return 0.0;
	const UWorld* World = QM->GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void FQuestPIEDebugChannel::BeginNewSession()
{
	FQuestStateSessionSnapshot NewSession;
	NewSession.SessionNumber = NextSessionNumber++;
	NewSession.SessionStartRealTime = FPlatformTime::Seconds();
	NewSession.bInFlight = true;
	SessionHistory.Add(MoveTemp(NewSession));

	// FIFO trim - drop oldest until under the cap. RemoveAt(0) preserves chronological index order.
	while (SessionHistory.Num() > MaxStoredSessions)
	{
		SessionHistory.RemoveAt(0);
	}

	UE_LOG(LogSimpleQuest, Verbose, TEXT("FQuestPIEDebugChannel::BeginNewSession : session #%d started, %d total in history"),
		SessionHistory.Last().SessionNumber, SessionHistory.Num());

	OnSessionHistoryChanged.Broadcast();
}

void FQuestPIEDebugChannel::FinalizeInFlightSession()
{
	if (SessionHistory.IsEmpty()) return;
	FQuestStateSessionSnapshot& Latest = SessionHistory.Last();
	if (!Latest.bInFlight) return;

	if (const UQuestStateSubsystem* QS = CachedQuestState.Get())
	{
		Latest.Resolutions = QS->GetAllResolutions();
		Latest.Entries = QS->GetAllEntries();
		Latest.PrereqStatus = QS->GetAllCachedPrereqStatus();
	}
	if (const UQuestManagerSubsystem* QM = CachedQuestManager.Get())
	{
		if (const UWorld* World = QM->GetWorld())
		{
			Latest.EndedAtGameTime = World->GetTimeSeconds();
		}
	}
	Latest.bInFlight = false;

	UE_LOG(LogSimpleQuest, Verbose, TEXT("FQuestPIEDebugChannel::FinalizeInFlightSession : session #%d closed at t=%.2fs (resolutions=%d, entries=%d, prereqs=%d)"),
		Latest.SessionNumber, Latest.EndedAtGameTime, Latest.Resolutions.Num(), Latest.Entries.Num(), Latest.PrereqStatus.Num());

	OnSessionHistoryChanged.Broadcast();
}

void FQuestPIEDebugChannel::HandleAnyRegistryChanged()
{
	OnSessionHistoryChanged.Broadcast();
}

const FQuestStateSessionSnapshot* FQuestPIEDebugChannel::GetSessionByIndex(int32 Index) const
{
	return SessionHistory.IsValidIndex(Index) ? &SessionHistory[Index] : nullptr;
}

const TMap<FGameplayTag, FQuestResolutionRecord>& FQuestPIEDebugChannel::GetResolutionsForSession(int32 Index) const
{
	static const TMap<FGameplayTag, FQuestResolutionRecord> Empty;
	if (!SessionHistory.IsValidIndex(Index)) return Empty;
	const FQuestStateSessionSnapshot& Session = SessionHistory[Index];
	if (Session.bInFlight)
	{
		const UQuestStateSubsystem* QS = CachedQuestState.Get();
		return QS ? QS->GetAllResolutions() : Empty;
	}
	return Session.Resolutions;
}

const TMap<FGameplayTag, FQuestEntryRecord>& FQuestPIEDebugChannel::GetEntriesForSession(int32 Index) const
{
	static const TMap<FGameplayTag, FQuestEntryRecord> Empty;
	if (!SessionHistory.IsValidIndex(Index)) return Empty;
	const FQuestStateSessionSnapshot& Session = SessionHistory[Index];
	if (Session.bInFlight)
	{
		const UQuestStateSubsystem* QS = CachedQuestState.Get();
		return QS ? QS->GetAllEntries() : Empty;
	}
	return Session.Entries;
}

const TMap<FGameplayTag, FQuestPrereqStatus>& FQuestPIEDebugChannel::GetPrereqStatusForSession(int32 Index) const
{
	static const TMap<FGameplayTag, FQuestPrereqStatus> Empty;
	if (!SessionHistory.IsValidIndex(Index)) return Empty;
	const FQuestStateSessionSnapshot& Session = SessionHistory[Index];
	if (Session.bInFlight)
	{
		const UQuestStateSubsystem* QS = CachedQuestState.Get();
		return QS ? QS->GetAllCachedPrereqStatus() : Empty;
	}
	return Session.PrereqStatus;
}
