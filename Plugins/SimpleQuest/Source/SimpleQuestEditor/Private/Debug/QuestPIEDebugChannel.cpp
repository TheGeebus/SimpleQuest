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

	// Always begin a new session even if QuestState didn't resolve — the snapshot fields stay empty in that case
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
	// Finalize before resetting CachedQuestState — finalize reads the live subsystem to capture registry maps.
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

	// Primary path: GEditor->PlayWorld — canonical access to the PIE/SIE world while play is active. Non-null for both
	// Play In Editor and Simulate In Editor. Avoids the world-context iteration edge cases (RunAsDedicated semantics,
	// multi-instance PIE) that complicate the loop-based path.
	UWorld* PIEWorld = GEditor->PlayWorld;

	if (!PIEWorld)
	{
		// Fallback: iterate world contexts looking for any PIE-type world. Covers edge cases where PlayWorld isn't set
		// but a PIE context exists (rare — certain dedicated-server-only startup flows).
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

	// Priority order: PendingGiver > Live > Completed > Deactivated — current activity outranks history. The
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

bool FQuestPIEDebugChannel::TryGetPrereqStatusForNode(const UEdGraphNode* EditorNode, FQuestPrereqStatus& OutStatus) const
{
	if (!IsActive()) return false;

	const FGameplayTag RuntimeTag = ResolveRuntimeTag(EditorNode);
	if (!RuntimeTag.IsValid())
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("TryGetPrereqStatusForNode: no runtime tag for editor node '%s' (class=%s)"),
			EditorNode ? *EditorNode->GetName() : TEXT("(null)"),
			EditorNode ? *EditorNode->GetClass()->GetName() : TEXT("(null)"));
		return false;
	}

	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	UQuestStateSubsystem* StateSubsystem = CachedQuestState.Get();
	UQuestManagerSubsystem* Manager = CachedQuestManager.Get();
	if (!WorldState || !StateSubsystem || !Manager)
	{
		return false;
	}

	const UQuestNodeBase* Instance = Manager->FindNodeInstance(RuntimeTag);
	if (!Instance)
	{
		// The opened asset can be a standalone questline whose placement runs under a parent's namespace. Facts publish on
		// every perspective, so ResolveRuntimeTag's own contextual swap does not fire here — but the instance registry is
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
	if (!Instance)
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("TryGetPrereqStatusForNode: no instance for '%s' under any perspective"), *RuntimeTag.ToString());
		return false;
	}

	OutStatus = Instance->GetPrerequisiteStatus(WorldState, StateSubsystem);
	return true;
}

EPrereqDebugState FQuestPIEDebugChannel::QueryLeafStateForSource(const UEdGraphNode* OwnerNode, FGameplayTag SourceTag, FName PathIdentity, bool bAnyOutcome) const
{
	FQuestPrereqStatus Status;
	if (!TryGetPrereqStatusForNode(OwnerNode, Status) || Status.bIsAlways) return EPrereqDebugState::Unknown;
	if (!SourceTag.IsValid()) return EPrereqDebugState::Unknown;

	UQuestStateSubsystem* StateSubsystem = CachedQuestState.Get();
	if (!StateSubsystem) return EPrereqDebugState::Unknown;

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
	return bAnySatisfied ? EPrereqDebugState::Satisfied : EPrereqDebugState::Unsatisfied;
}

FGameplayTag FQuestPIEDebugChannel::ResolveRuntimeTag(const UEdGraphNode* EditorNode) const
{
	if (!EditorNode) return FGameplayTag();

	const UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(EditorNode);
	if (!ContentNode) return FGameplayTag();

	// Resolve own-asset compile tag via Outer walk + CompiledNodes lookup (same as before).
	UObject* Outer = EditorNode->GetGraph() ? EditorNode->GetGraph()->GetOuter() : nullptr;
	while (Outer && !Outer->IsA<UQuestlineGraph>()) Outer = Outer->GetOuter();
	const UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(Outer);
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

	// Contextual resolution: if PIE is running and own-asset tag has no live state, the asset may be opened
	// while a parent LinkedQuestline placement is the actively running instance. Consult the state subsystem's
	// runtime alias index (ResolveCanonicalTags) instead of the editor-utility's asset-registry walk — the
	// runtime index reflects post-game-start registrations (including listener auto-load), whereas the asset-
	// registry walk only sees compile-time data and can disagree with what the manager has actually registered.
	// Closes the "halo doesn't update post-listener" symptom.
	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	UQuestStateSubsystem* StateSubsystem = CachedQuestState.Get();
	if (WorldState && StateSubsystem && OwnAssetTag.IsValid() && !HasAnyStateFact(OwnAssetTag, WorldState))
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

	// FIFO trim — drop oldest until under the cap. RemoveAt(0) preserves chronological index order.
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
