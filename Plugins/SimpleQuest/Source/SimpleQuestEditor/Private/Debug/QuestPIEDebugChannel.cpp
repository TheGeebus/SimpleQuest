// Copyright 2026, Greg Bussell, All Rights Reserved.

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
#include "WorldState/WorldStateSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
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
	// (QueryNodeState, QueryLeafState, HasFact). QuestState is a separately-checked optional resource for the
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

	// Priority order: Blocked > Completed > PendingGiver > Live > Deactivated
	const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(RuntimeTag, EQuestStateLeaf::Blocked);
	if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact)) return EQuestNodeDebugState::Blocked;
		
	const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(RuntimeTag, EQuestStateLeaf::Completed);
	if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact)) return EQuestNodeDebugState::Completed;
	
	const FGameplayTag PendingGiverFact = FQuestTagComposer::ResolveStateFactTag(RuntimeTag, EQuestStateLeaf::PendingGiver);
	if (PendingGiverFact.IsValid() && WorldState->HasFact(PendingGiverFact)) return EQuestNodeDebugState::PendingGiver;
	
	const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(RuntimeTag, EQuestStateLeaf::Live);
	if (LiveFact.IsValid() && WorldState->HasFact(LiveFact)) return EQuestNodeDebugState::Live;

	const FGameplayTag DeactivatedFact = FQuestTagComposer::ResolveStateFactTag(RuntimeTag, EQuestStateLeaf::Deactivated);
	if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact)) return EQuestNodeDebugState::Deactivated;
	
	return EQuestNodeDebugState::Unknown;
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

	// Contextual resolution: if PIE is running and own-asset tag has no live state, the asset is probably opened
	// while a parent LinkedQuestline nesting is active. Walk contextual parents; return the first with live state.
	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	if (WorldState && OwnAssetTag.IsValid() && !HasAnyStateFact(OwnAssetTag, WorldState))
	{
		for (const FGameplayTag& ContextualTag : FSimpleQuestEditorUtilities::CollectContextualNodeTagsForEditorNode(ContentNode))
		{
			if (HasAnyStateFact(ContextualTag, WorldState)) return ContextualTag;
		}
	}
	return OwnAssetTag;
}

EPrereqDebugState FQuestPIEDebugChannel::QueryLeafState(const FGameplayTag& LeafFact, const FGameplayTag& SourceRuntimeTag, const UQuestlineNode_ContentBase* LeafSourceNode) const
{
	if (!IsActive() || !LeafFact.IsValid() || !SourceRuntimeTag.IsValid()) return EPrereqDebugState::Unknown;
	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	if (!WorldState) return EPrereqDebugState::Unknown;

	// Contextual rewrite: if the leaf fact isn't present under the own-asset tag but the leaf's source node has a
	// live contextual nesting, rewrite both LeafFact and SourceRuntimeTag to use that nesting. Lets the examiner
	// light up when viewing a linked asset's graph while a parent graph is actively running it.
	FGameplayTag EffectiveLeaf = LeafFact;
	FGameplayTag EffectiveSource = SourceRuntimeTag;
	if (LeafSourceNode && !WorldState->HasFact(LeafFact) && !HasAnyStateFact(SourceRuntimeTag, WorldState))
	{
	    for (const FGameplayTag& ContextualSource : FSimpleQuestEditorUtilities::CollectContextualNodeTagsForEditorNode(LeafSourceNode))
	    {
	        if (!HasAnyStateFact(ContextualSource, WorldState)) continue;

	    	// Source nesting is live. Rewrite the leaf fact by transplanting the leaf-fact tail onto the contextual
	    	// source: strip "SimpleQuest.QuestState.<home>." prefix off the leaf, re-root under
	    	// "SimpleQuest.QuestState.<contextual>.".
	    	const FString LeafStr = LeafFact.GetTagName().ToString();
	    	const FName HomeStateRoot = FQuestTagComposer::SwapNamespacePrefix(SourceRuntimeTag.GetTagName(), FQuestTagComposer::IdentityNamespace, FQuestTagComposer::StateNamespace);
	    	const FString HomeStatePrefix = HomeStateRoot.ToString();
	    	if (LeafStr.StartsWith(HomeStatePrefix))
	    	{
	    		const FString Suffix = LeafStr.RightChop(HomeStatePrefix.Len()); // ".Path.Reached" etc.
	    		const FName ContextualStateRoot = FQuestTagComposer::SwapNamespacePrefix(ContextualSource.GetTagName(),	FQuestTagComposer::IdentityNamespace, FQuestTagComposer::StateNamespace);
	    		const FGameplayTag RewrittenLeaf = FGameplayTag::RequestGameplayTag(FName(*(ContextualStateRoot.ToString() + Suffix)), false);
	            if (RewrittenLeaf.IsValid())
	            {
	                EffectiveLeaf = RewrittenLeaf;
	                EffectiveSource = ContextualSource;
	                break;
	            }
	        }
	    }
	}
	
	// Satisfied wins regardless of source state: the fact is present, the leaf evaluates true.
	if (WorldState->HasFact(EffectiveLeaf)) return EPrereqDebugState::Satisfied;

	// Classify based on the source content node's state facts. Resolve each SimpleQuest.QuestState.<SourceTag>.<Leaf>
	// lazily; the ones we actually check short-circuit on first hit.
	auto LookupSourceFact = [&](EQuestStateLeaf Leaf) -> bool
	{
		const FGameplayTag Tag = FQuestTagComposer::ResolveStateFactTag(EffectiveSource, Leaf);
		return Tag.IsValid() && WorldState->HasFact(Tag);
	};

	const bool bSourceLive = LookupSourceFact(EQuestStateLeaf::Live);
	const bool bSourcePendingGiver = LookupSourceFact(EQuestStateLeaf::PendingGiver);
	const bool bSourceCompleted = LookupSourceFact(EQuestStateLeaf::Completed);
	const bool bSourceDeactivated = LookupSourceFact(EQuestStateLeaf::Deactivated);

	if (bSourceCompleted || bSourceDeactivated)
	{
		// Source resolved but the leaf's specific fact isn't present: the leaf's required outcome won't happen.
		return EPrereqDebugState::Unsatisfied;
	}
	if (bSourceLive || bSourcePendingGiver)
	{
		// Source running, outcome not yet resolved: still in flight.
		return EPrereqDebugState::InProgress;
	}
	// No state facts at all: source hasn't activated in this PIE session.
	return EPrereqDebugState::NotStarted;
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
