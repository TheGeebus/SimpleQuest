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
#include "Utilities/QuestStateTagUtils.h"

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
	CachedWorldState.Reset();
	CachedQuestManager.Reset();
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
	OnDebugActiveChanged.Broadcast();
}

void FQuestPIEDebugChannel::HandleEndPIE(bool bIsSimulating)
{
	CachedWorldState.Reset();
	CachedQuestManager.Reset();
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

	UE_LOG(LogSimpleQuest, Display, TEXT("FQuestPIEDebugChannel::ResolvePIESubsystems : world='%s' type=%d, WorldState=%s, QuestManager=%s"),
		*PIEWorld->GetName(), static_cast<int32>(PIEWorld->WorldType),
		CachedWorldState.IsValid() ? TEXT("resolved") : TEXT("NULL"),
		CachedQuestManager.IsValid() ? TEXT("resolved") : TEXT("NULL"));

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

	// Priority order: Blocked > PendingGiver > Active > Completed > Deactivated. See agenda item 7 Session A discussion.
	const FName TagName = RuntimeTag.GetTagName();
	const FGameplayTag BlockedFact = FGameplayTag::RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_Blocked), /*ErrorIfNotFound*/ false);
	if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact)) return EQuestNodeDebugState::Blocked;

	const FGameplayTag PendingGiverFact = FGameplayTag::RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_PendingGiver), false);
	if (PendingGiverFact.IsValid() && WorldState->HasFact(PendingGiverFact)) return EQuestNodeDebugState::PendingGiver;

	const FGameplayTag ActiveFact = FGameplayTag::RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_Active), false);
	if (ActiveFact.IsValid() && WorldState->HasFact(ActiveFact)) return EQuestNodeDebugState::Active;

	const FGameplayTag CompletedFact = FGameplayTag::RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_Completed), false);
	if (CompletedFact.IsValid() && WorldState->HasFact(CompletedFact)) return EQuestNodeDebugState::Completed;

	const FGameplayTag DeactivatedFact = FGameplayTag::RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_Deactivated), false);
	if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact)) return EQuestNodeDebugState::Deactivated;
	
	return EQuestNodeDebugState::Unknown;
}

FGameplayTag FQuestPIEDebugChannel::ResolveRuntimeTag(const UEdGraphNode* EditorNode)
{
	if (!EditorNode) return FGameplayTag();

	const UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(EditorNode);
	if (!ContentNode) return FGameplayTag();

	// Walk up Outer chain to find the owning UQuestlineGraph — same pattern used by FindCompiledTagForNode.
	UObject* Outer = EditorNode->GetGraph() ? EditorNode->GetGraph()->GetOuter() : nullptr;
	while (Outer && !Outer->IsA<UQuestlineGraph>()) Outer = Outer->GetOuter();

	const UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(Outer);
	if (!QuestlineAsset) return FGameplayTag();

	for (const auto& [TagName, NodeInstance] : QuestlineAsset->GetCompiledNodes())
	{
		if (NodeInstance && NodeInstance->GetQuestGuid() == ContentNode->QuestGuid)
		{
			return FGameplayTag::RequestGameplayTag(TagName, false);
		}
	}
	return FGameplayTag();
}

EPrereqDebugState FQuestPIEDebugChannel::QueryLeafState(const FGameplayTag& LeafFact, const FGameplayTag& SourceRuntimeTag) const
{
	if (!IsActive() || !LeafFact.IsValid() || !SourceRuntimeTag.IsValid()) return EPrereqDebugState::Unknown;
	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	if (!WorldState) return EPrereqDebugState::Unknown;

	// Satisfied wins regardless of source state — the fact is present, the leaf evaluates true.
	if (WorldState->HasFact(LeafFact)) return EPrereqDebugState::Satisfied;

	// Classify based on the source content node's state facts. Resolve each QuestState.<SourceTag>.<Leaf> lazily; the
	// ones we actually check short-circuit on first hit.
	const FName SourceTagName = SourceRuntimeTag.GetTagName();
	auto LookupSourceFact = [&](const FString& Leaf) -> bool
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FQuestStateTagUtils::MakeStateFact(SourceTagName, Leaf), /*ErrorIfNotFound*/ false);
		return Tag.IsValid() && WorldState->HasFact(Tag);
	};

	const bool bSourceActive       = LookupSourceFact(FQuestStateTagUtils::Leaf_Active);
	const bool bSourcePendingGiver = LookupSourceFact(FQuestStateTagUtils::Leaf_PendingGiver);
	const bool bSourceCompleted    = LookupSourceFact(FQuestStateTagUtils::Leaf_Completed);
	const bool bSourceDeactivated  = LookupSourceFact(FQuestStateTagUtils::Leaf_Deactivated);

	if (bSourceCompleted || bSourceDeactivated)
	{
		// Source resolved but the leaf's specific fact isn't present — the leaf's required outcome won't happen.
		return EPrereqDebugState::Unsatisfied;
	}
	if (bSourceActive || bSourcePendingGiver)
	{
		// Source running, outcome not yet resolved — still in flight.
		return EPrereqDebugState::InProgress;
	}
	// No state facts at all — source hasn't activated in this PIE session.
	return EPrereqDebugState::NotStarted;
}

bool FQuestPIEDebugChannel::HasFact(const FGameplayTag& FactTag) const
{
	if (!IsActive() || !FactTag.IsValid()) return false;
	UWorldStateSubsystem* WorldState = CachedWorldState.Get();
	return WorldState && WorldState->HasFact(FactTag);
}