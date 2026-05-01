// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Subsystems/QuestStateSubsystem.h"

#include "Engine/GameInstance.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestResolutionRecordedEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "WorldState/WorldStateSubsystem.h"

const FQuestResolutionRecord* UQuestStateSubsystem::GetQuestResolution(FGameplayTag QuestTag) const
{
	return QuestResolutions.Find(QuestTag);
}

bool UQuestStateSubsystem::HasResolved(FGameplayTag QuestTag) const
{
	return QuestResolutions.Contains(QuestTag);
}

bool UQuestStateSubsystem::HasResolvedWith(FGameplayTag QuestTag, FGameplayTag OutcomeTag) const
{
	if (!QuestTag.IsValid() || !OutcomeTag.IsValid()) return false;
	const TSet<FGameplayTag>* OutcomeSet = ResolvedOutcomesByQuest.Find(QuestTag);
	return OutcomeSet && OutcomeSet->Contains(OutcomeTag);
}

int32 UQuestStateSubsystem::GetResolutionCount(FGameplayTag QuestTag) const
{
	const FQuestResolutionRecord* Record = QuestResolutions.Find(QuestTag);
	return Record ? Record->GetCount() : 0;
}

TArray<FQuestResolutionEntry> UQuestStateSubsystem::GetResolutionHistory(FGameplayTag QuestTag) const
{
	if (const FQuestResolutionRecord* Record = QuestResolutions.Find(QuestTag))
	{
		return Record->History;
	}
	return TArray<FQuestResolutionEntry>();
}

FQuestResolutionEntry UQuestStateSubsystem::GetLatestResolution(FGameplayTag QuestTag) const
{
	if (const FQuestResolutionRecord* Record = QuestResolutions.Find(QuestTag))
	{
		if (const FQuestResolutionEntry* Latest = Record->GetLatest())
		{
			return *Latest;
		}
	}
	return FQuestResolutionEntry();
}

TArray<FQuestActivationBlocker> UQuestStateSubsystem::QueryQuestActivationBlockers(FGameplayTag QuestTag) const
{
	TArray<FQuestActivationBlocker> Out;

	// 1. UnknownQuest — early return; no other checks meaningful for unregistered tags.
	if (!FQuestTagComposer::IsTagRegisteredInRuntime(QuestTag))
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
		FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Live), false)))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::AlreadyLive;
		Out.Add(Blocker);
	}

	// 3. Blocked — terminal until ClearBlocked.
	if (WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(
		FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Blocked), false)))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::Blocked;
		Out.Add(Blocker);
	}

	// 4. Deactivated — clearable on Activate-input re-entry.
	if (WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(
		FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::Deactivated), false)))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::Deactivated;
		Out.Add(Blocker);
	}

	// 5. NotPendingGiver — quest hasn't been activated to giver-offer state.
	if (!WS->HasFact(UGameplayTagsManager::Get().RequestGameplayTag(
		FQuestTagComposer::MakeStateFact(QuestTag, EQuestStateLeaf::PendingGiver), false)))
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

void UQuestStateSubsystem::RecordResolution(FGameplayTag QuestTag, FGameplayTag OutcomeTag, double ResolutionTime, EQuestResolutionSource Source)
{
	if (!QuestTag.IsValid()) return;

	FQuestResolutionRecord& Record = QuestResolutions.FindOrAdd(QuestTag);
	FQuestResolutionEntry& Entry = Record.History.Emplace_GetRef();
	Entry.OutcomeTag = OutcomeTag;
	Entry.ResolutionTime = ResolutionTime;
	Entry.Source = Source;

	// Index maintenance for HasResolvedWith. Skipped when OutcomeTag is invalid (the "resolve without specifying an
	// outcome" case via the BP ResolveQuest helper). Those entries appear in History but don't contribute to outcome-
	// keyed lookups. TSet handles deduplication so repeat resolutions with the same outcome don't bloat the set.
	if (OutcomeTag.IsValid())
	{
		ResolvedOutcomesByQuest.FindOrAdd(QuestTag).Add(OutcomeTag);
	}

	UE_LOG(LogSimpleQuest, Log, TEXT("QuestResolutions: appended '%s' outcome='%s' source=%s (resolution #%d at t=%.2fs)"),
		*QuestTag.ToString(),
		*OutcomeTag.ToString(),
		Source == EQuestResolutionSource::External ? TEXT("External") : TEXT("Graph"),
		Record.History.Num(),
		ResolutionTime);

	// Broadcast on the resolved quest's tag channel. Distinct from FQuestEndedEvent (manager-published in
	// ChainToNextNodes for graph-driven completions). This event fires for every resolution path - graph chain,
	// external ResolveQuest, future save rehydration - so subscribers reach a single canonical channel.
	if (USignalSubsystem* Signals = ResolveSignalSubsystem())
	{
		Signals->PublishMessage(QuestTag, FQuestResolutionRecordedEvent(QuestTag, OutcomeTag, ResolutionTime, Source));
	}
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

USignalSubsystem* UQuestStateSubsystem::ResolveSignalSubsystem() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<USignalSubsystem>();
	}
	return nullptr;
}
