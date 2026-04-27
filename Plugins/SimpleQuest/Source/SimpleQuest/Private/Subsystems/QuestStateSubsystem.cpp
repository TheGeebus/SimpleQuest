// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Subsystems/QuestStateSubsystem.h"

#include "Engine/GameInstance.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Utilities/QuestStateTagUtils.h"
#include "WorldState/WorldStateSubsystem.h"

const FQuestResolutionRecord* UQuestStateSubsystem::GetQuestResolution(FGameplayTag QuestTag) const
{
	return QuestResolutions.Find(QuestTag);
}

bool UQuestStateSubsystem::HasResolved(FGameplayTag QuestTag) const
{
	return QuestResolutions.Contains(QuestTag);
}

int32 UQuestStateSubsystem::GetResolutionCount(FGameplayTag QuestTag) const
{
	const FQuestResolutionRecord* Record = QuestResolutions.Find(QuestTag);
	return Record ? Record->ResolutionCount : 0;
}

TArray<FQuestActivationBlocker> UQuestStateSubsystem::QueryQuestActivationBlockers(FGameplayTag QuestTag) const
{
	TArray<FQuestActivationBlocker> Out;

	// 1. UnknownQuest — early return; no other checks meaningful for unregistered tags.
	if (!FQuestStateTagUtils::IsTagRegisteredInRuntime(QuestTag))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::UnknownQuest;
		Out.Add(Blocker);
		return Out;
	}

	UWorldStateSubsystem* WS = ResolveWorldState();
	if (!WS) return Out;

	// State-fact blockers (in declared priority order — designer can early-return on first match).

	// 2. AlreadyLive — terminal: quest in flight.
	if (WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(
		FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live), false)))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::AlreadyLive;
		Out.Add(Blocker);
	}

	// 3. Blocked — terminal until ClearBlocked.
	if (WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(
		FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Blocked), false)))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::Blocked;
		Out.Add(Blocker);
	}

	// 4. Deactivated — clearable on Activate-input re-entry.
	if (WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(
		FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Deactivated), false)))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::Deactivated;
		Out.Add(Blocker);
	}

	// 5. NotPendingGiver — quest hasn't been activated to giver-offer state.
	if (!WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(
		FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver), false)))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::NotPendingGiver;
		Out.Add(Blocker);
	}

	// 6. PrereqUnmet — read cached prereq status. Manager pushes this on giver-branch entry and on
	//    enablement-watch transitions, so the cache reflects the current evaluation.
	if (const FQuestPrereqStatus* Cached = CachedPrereqStatus.Find(QuestTag))
	{
		if (!Cached->bIsAlways && !Cached->bSatisfied)
		{
			FQuestActivationBlocker Blocker;
			Blocker.Reason = EQuestActivationBlocker::PrereqUnmet;
			for (const FQuestPrereqLeafStatus& Leaf : Cached->Leaves)
			{
				if (!Leaf.bSatisfied) Blocker.UnsatisfiedLeafTags.Add(Leaf.LeafTag);
			}
			Out.Add(Blocker);
		}
	}

	return Out;
}

FQuestPrereqStatus UQuestStateSubsystem::GetQuestPrereqStatus(FGameplayTag QuestTag) const
{
	if (const FQuestPrereqStatus* Cached = CachedPrereqStatus.Find(QuestTag))
	{
		return *Cached;
	}
	return FQuestPrereqStatus();
}

void UQuestStateSubsystem::RecordResolution(FGameplayTag QuestTag, FGameplayTag OutcomeTag, double ResolutionTime)
{
	if (!QuestTag.IsValid()) return;

	FQuestResolutionRecord& Record = QuestResolutions.FindOrAdd(QuestTag);
	Record.OutcomeTag = OutcomeTag;
	Record.ResolutionTime = ResolutionTime;
	Record.ResolutionCount++;

	UE_LOG(LogSimpleQuest, Log, TEXT("QuestResolutions: recorded '%s' outcome='%s' (resolution #%d at t=%.2fs)"),
		*QuestTag.ToString(), *OutcomeTag.ToString(), Record.ResolutionCount, Record.ResolutionTime);
}

void UQuestStateSubsystem::UpdateQuestPrereqStatus(FGameplayTag QuestTag, const FQuestPrereqStatus& Status)
{
	if (!QuestTag.IsValid()) return;
	CachedPrereqStatus.Add(QuestTag, Status);
	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestStateSubsystem::UpdateQuestPrereqStatus : '%s' bSatisfied=%d (leaves=%d)"),
		*QuestTag.ToString(), Status.bSatisfied ? 1 : 0, Status.Leaves.Num());
}

void UQuestStateSubsystem::ClearQuestPrereqStatus(FGameplayTag QuestTag)
{
	if (CachedPrereqStatus.Remove(QuestTag) > 0)
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestStateSubsystem::ClearQuestPrereqStatus : '%s' cleared"),
			*QuestTag.ToString());
	}
}

UWorldStateSubsystem* UQuestStateSubsystem::ResolveWorldState() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UWorldStateSubsystem>();
	}
	return nullptr;
}