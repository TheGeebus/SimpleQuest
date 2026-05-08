// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Subsystems/QuestStateSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestEntryRecordedEvent.h"
#include "Events/QuestResolutionRecordedEvent.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestTagComposer.h"
#include "WorldState/WorldStateSubsystem.h"


namespace
{
	/**
	 * Multi-publish helper — fires Event on CanonicalTag and on each AssetScopedAliasTag mapped to it via the
	 * reverse alias index. Each publish gets its own copy of Event with Event.QuestTag set to the channel; the
	 * by-value template parameter ensures per-channel mutation doesn't leak back to the caller. Sibling pattern
	 * to FQuestPublish::OnAllNodeTags but for the state subsystem, which doesn't carry node references.
	 */
	template <typename EventType>
	void PublishWithAliases(USignalSubsystem* Signals, FGameplayTag CanonicalTag, const TMap<FGameplayTag, TArray<FGameplayTag>>& AliasReverseMap, EventType Event)
	{
		if (!Signals || !CanonicalTag.IsValid()) return;

		Event.QuestTag = CanonicalTag;
		Signals->PublishMessage(CanonicalTag, Event);

		if (const TArray<FGameplayTag>* Aliases = AliasReverseMap.Find(CanonicalTag))
		{
			for (const FGameplayTag& AliasTag : *Aliases)
			{
				if (AliasTag.IsValid())
				{
					Event.QuestTag = AliasTag;
					Signals->PublishMessage(AliasTag, Event);
				}
			}
		}
	}
}

const FQuestResolutionRecord* UQuestStateSubsystem::GetQuestResolution(FGameplayTag QuestTag) const
{
	return QuestResolutions.Find(QuestTag);
}

bool UQuestStateSubsystem::HasResolved(FGameplayTag QuestTag) const
{
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (QuestResolutions.Contains(Tag)) return true;
	}
	return false;
}

bool UQuestStateSubsystem::HasResolvedWith(FGameplayTag QuestTag, FGameplayTag OutcomeTag) const
{
	if (!QuestTag.IsValid() || !OutcomeTag.IsValid()) return false;
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (const TSet<FGameplayTag>* OutcomeSet = ResolvedOutcomesByQuest.Find(Tag))
		{
			if (OutcomeSet->Contains(OutcomeTag)) return true;
		}
	}
	return false;
}

int32 UQuestStateSubsystem::GetResolutionCount(FGameplayTag QuestTag) const
{
	int32 Total = 0;
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (const FQuestResolutionRecord* Record = QuestResolutions.Find(Tag))
		{
			Total += Record->GetCount();
		}
	}
	return Total;
}

TArray<FQuestResolutionEntry> UQuestStateSubsystem::GetResolutionHistory(FGameplayTag QuestTag) const
{
	TArray<FQuestResolutionEntry> Result;
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (const FQuestResolutionRecord* Record = QuestResolutions.Find(Tag))
		{
			Result.Append(Record->History);
		}
	}
	return Result;
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

const FQuestEntryRecord* UQuestStateSubsystem::GetQuestEntry(FGameplayTag QuestTag) const
{
	return QuestEntries.Find(QuestTag);
}

bool UQuestStateSubsystem::HasEntered(FGameplayTag QuestTag) const
{
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (QuestEntries.Contains(Tag)) return true;
	}
	return false;
}

bool UQuestStateSubsystem::HasEnteredWith(FGameplayTag QuestTag, FGameplayTag IncomingOutcomeTag) const
{
	if (!QuestTag.IsValid() || !IncomingOutcomeTag.IsValid()) return false;
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (const TSet<FGameplayTag>* OutcomeSet = EnteredOutcomesByQuest.Find(Tag))
		{
			if (OutcomeSet->Contains(IncomingOutcomeTag)) return true;
		}
	}
	return false;
}

int32 UQuestStateSubsystem::GetEntryCount(FGameplayTag QuestTag) const
{
	int32 Total = 0;
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (const FQuestEntryRecord* Record = QuestEntries.Find(Tag))
		{
			Total += Record->GetCount();
		}
	}
	return Total;
}

TArray<FQuestEntryArrival> UQuestStateSubsystem::GetEntryHistory(FGameplayTag QuestTag) const
{
	TArray<FQuestEntryArrival> Result;
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (const FQuestEntryRecord* Record = QuestEntries.Find(Tag))
		{
			Result.Append(Record->History);
		}
	}
	return Result;
}

FQuestEntryArrival UQuestStateSubsystem::GetLatestEntry(FGameplayTag QuestTag) const
{
	if (const FQuestEntryRecord* Record = QuestEntries.Find(QuestTag))
	{
		if (const FQuestEntryArrival* Latest = Record->GetLatest())
		{
			return *Latest;
		}
	}
	return FQuestEntryArrival();
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

	// Resolve canonical placements so quest-by-name lifecycle checks consider every active placement (standalone
	// + every aliased contextual). Without this, a quest reached only through a LinkedQuestline can never be given
	// — the giver fires against the standalone-perspective tag (which has no PendingGiver fact set when only the
	// inlined placement is active), and an exact-match check would refuse the give with NotPendingGiver despite
	// the inlined placement being ready to receive. Top-level / non-aliased input collapses to a single iteration.
	const TArray<FGameplayTag> CanonicalTags = ResolveCanonicalTags(QuestTag);
	auto AnyCanonical = [&CanonicalTags](TFunctionRef<bool(const FGameplayTag&)> Predicate) -> bool
	{
		for (const FGameplayTag& CanonicalTag : CanonicalTags)
		{
			if (Predicate(CanonicalTag)) return true;
		}
		return false;
	};

	// State-fact blockers (in declared priority order — designer can early-return on first match).

	// 2. AlreadyLive — terminal for Steps. Containers (UQuest wrappers) are exempt: their Live fact is derived
	//    from inner Step state, so a give forwarding activation to a Live wrapper with mixed-Live inner Steps is
	//    valid — exactly the path the path-aware giver gate targets. Step Live blocks because Steps own
	//    their Live state directly and re-activation while Live would corrupt lifecycle invariants.
	if (AnyCanonical([WS, this](const FGameplayTag& Tag) { return FQuestLifecycleQuery::IsLive(WS, Tag) && !IsContainerTag(Tag); }))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::AlreadyLive;
		Out.Add(Blocker);
	}

	// 3. Blocked — terminal until ClearBlocked.
	if (AnyCanonical([WS](const FGameplayTag& Tag) { return FQuestLifecycleQuery::IsBlocked(WS, Tag); }))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::Blocked;
		Out.Add(Blocker);
	}

	// 4. NotPendingGiver — quest hasn't been activated to giver-offer state on any placement. The give can target
	//    whichever placement is in PendingGiver; only refuse when none is.
	if (!AnyCanonical([WS](const FGameplayTag& Tag) { return FQuestLifecycleQuery::IsPendingGiver(WS, Tag); }))
	{
		FQuestActivationBlocker Blocker;
		Blocker.Reason = EQuestActivationBlocker::NotPendingGiver;
		Out.Add(Blocker);
	}

	// 5. PrereqUnmet — read cached prereq status. Manager pushes this on giver-branch entry and on enablement-
	//    watch transitions, so the cache reflects the current evaluation. Walk canonicals so the cache entry from
	//    whichever placement was activated is found, even when the give event fires against the standalone-form
	//    alias and the inlined placement holds the cached status.
	for (const FGameplayTag& CanonicalTag : CanonicalTags)
	{
		const FQuestPrereqStatus* Cached = CachedPrereqStatus.Find(CanonicalTag);
		if (Cached && !Cached->bIsAlways && !Cached->bSatisfied)
		{
			FQuestActivationBlocker Blocker;
			Blocker.Reason = EQuestActivationBlocker::PrereqUnmet;
			for (const FQuestPrereqLeafStatus& Leaf : Cached->Leaves)
			{
				if (!Leaf.bSatisfied) Blocker.UnsatisfiedLeafTags.Add(Leaf.LeafTag);
			}
			Out.Add(Blocker);
			break;  // First unsatisfied placement reported; aggregate-leaf detail comes from that placement's cache.
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

	// Broadcast on the resolved quest's tag channel + each AssetScopedAliasTag for cross-asset subscribers.
	// Distinct from FQuestEndedEvent (manager-published in ChainToNextNodes for graph-driven completions). This
	// event fires for every resolution path — graph chain, external ResolveQuest, future save rehydration — so
	// subscribers reach a single canonical mechanism. Multi-publish via PublishWithAliases ensures Leaf_Resolution
	// prereq leaves and any other subscribers bound through alias tags receive the event on their natural channel.
	PublishWithAliases(
		ResolveSignalSubsystem(),
		QuestTag,
		AssetScopedAliasTagsByContextualTag,
		FQuestResolutionRecordedEvent(QuestTag, OutcomeTag, ResolutionTime, Source));

	OnAnyRegistryChanged.Broadcast();
}

void UQuestStateSubsystem::RecordEntry(
	FGameplayTag QuestTag,
	FGameplayTag SourceQuestTag,
	FGameplayTag IncomingOutcomeTag,
	double EntryTime,
	EQuestActivationProvenance Provenance,
	const FQuestObjectiveActivationParams& ActivationParamsSnapshot,
	FName PathIdentity)
{
	if (!QuestTag.IsValid()) return;

	FQuestEntryRecord& Record = QuestEntries.FindOrAdd(QuestTag);
	FQuestEntryArrival& Entry = Record.History.Emplace_GetRef();
	Entry.SourceQuestTag = SourceQuestTag;
	Entry.IncomingOutcomeTag = IncomingOutcomeTag;
	Entry.EntryTime = EntryTime;
	Entry.Provenance = Provenance;
	Entry.ActivationParamsSnapshot = ActivationParamsSnapshot;
	Entry.PathIdentity = PathIdentity;

	// Index maintenance for HasEnteredWith. Skipped when IncomingOutcomeTag is invalid (defensive — the cascade
	// path always carries a valid outcome tag in current code, but the registry stays robust against future paths
	// that might call RecordEntry without one).
	if (IncomingOutcomeTag.IsValid())
	{
		EnteredOutcomesByQuest.FindOrAdd(QuestTag).Add(IncomingOutcomeTag);
	}

	const AActor* GiverActor = ActivationParamsSnapshot.ActivationSource;
	UE_LOG(LogSimpleQuest, Log,
		TEXT("QuestEntries: appended '%s' source='%s' outcome='%s' provenance=%s giver='%s' path='%s' targetActors=%d targetClasses=%d numRequired=%d (entry #%d at t=%.2fs)"),
		*QuestTag.ToString(),
		*SourceQuestTag.ToString(),
		*IncomingOutcomeTag.ToString(),
		*UEnum::GetValueAsString(Provenance),
		GiverActor ? *GiverActor->GetName() : TEXT("null"),
		*PathIdentity.ToString(),
		ActivationParamsSnapshot.TargetActors.Num(),
		ActivationParamsSnapshot.TargetClasses.Num(),
		ActivationParamsSnapshot.NumElementsRequired,
		Record.History.Num(),
		EntryTime);

	// Broadcast on the destination quest's tag channel + each AssetScopedAliasTag. PrereqLeafSubscription consumers
	// routed by Leaf_Entry listen here and trigger expression re-evaluation; designers can also subscribe directly
	// for cascade-attribution audit / logging. The event's payload preserves the legacy (QuestTag, SourceQuestTag,
	// IncomingOutcomeTag, EntryTime) shape — subscribers wanting the new provenance / snapshot fields read the
	// latest entry from the registry on receipt. Multi-publish via PublishWithAliases ensures cross-asset
	// subscribers receive the event on their natural alias channel.
	PublishWithAliases(
		ResolveSignalSubsystem(),
		QuestTag,
		AssetScopedAliasTagsByContextualTag,
		FQuestEntryRecordedEvent(QuestTag, SourceQuestTag, IncomingOutcomeTag, EntryTime));

	OnAnyRegistryChanged.Broadcast();
}

TArray<FGameplayTag> UQuestStateSubsystem::GetQuestTagsUnderPrefix(FGameplayTag Prefix) const
{
	TArray<FGameplayTag> Out;
	if (!Prefix.IsValid()) return Out;
	Out.Reserve(KnownQuests.Num() + ContextualTagsByAssetScopedTag.Num());

	// ContextualTags from KnownQuests — the parent-context perspective on each compiled node.
	for (const TPair<FGameplayTag, FQuestRuntimeRecord>& Pair : KnownQuests)
	{
		// MatchesTag returns true when the iterated key is Prefix or a descendant of Prefix — the live signal
		// bus's hierarchical-walk semantic, applied to the registered-tag set rather than the publish stream.
		if (Pair.Key.MatchesTag(Prefix))
		{
			Out.AddUnique(Pair.Key);
		}
	}

	// AssetScopedAliasTags — the inner-asset perspective. Cross-asset subscribers binding to an alias-shape
	// prefix expect to enumerate descendants under that prefix too; without this walk, they'd miss alias-tag
	// matches that are only registered through the alias index.
	for (const TPair<FGameplayTag, TArray<FGameplayTag>>& Pair : ContextualTagsByAssetScopedTag)
	{
		if (Pair.Key.MatchesTag(Prefix))
		{
			Out.AddUnique(Pair.Key);
		}
	}

	return Out;
}

bool UQuestStateSubsystem::IsKnownQuestTag(FGameplayTag QuestTag) const
{
	if (!QuestTag.IsValid()) return false;
	if (KnownQuests.Contains(QuestTag)) return true;
	// Alias case — registered through the alias index even though not in KnownQuests directly.
	return ContextualTagsByAssetScopedTag.Contains(QuestTag);
}

int32 UQuestStateSubsystem::GetKnownQuestTagCount() const
{
	return KnownQuests.Num();
}

const FQuestRuntimeRecord* UQuestStateSubsystem::GetQuestRuntimeRecord(FGameplayTag QuestTag) const
{
	return KnownQuests.Find(QuestTag);
}

AActor* UQuestStateSubsystem::GetLastGiverActor(FGameplayTag QuestTag) const
{
	if (const FQuestEntryRecord* Record = QuestEntries.Find(QuestTag))
	{
		if (const FQuestEntryArrival* Latest = Record->GetLatest())
		{
			return Latest->ActivationParamsSnapshot.ActivationSource;
		}
	}
	return nullptr;
}

EQuestActivationProvenance UQuestStateSubsystem::GetLastActivationProvenance(FGameplayTag QuestTag) const
{
	if (const FQuestEntryRecord* Record = QuestEntries.Find(QuestTag))
	{
		if (const FQuestEntryArrival* Latest = Record->GetLatest())
		{
			return Latest->Provenance;
		}
	}
	return EQuestActivationProvenance::Unknown;
}

FQuestObjectiveActivationParams UQuestStateSubsystem::GetLastActivationParamsSnapshot(FGameplayTag QuestTag) const
{
	if (const FQuestEntryRecord* Record = QuestEntries.Find(QuestTag))
	{
		if (const FQuestEntryArrival* Latest = Record->GetLatest())
		{
			return Latest->ActivationParamsSnapshot;
		}
	}
	return FQuestObjectiveActivationParams();
}

FName UQuestStateSubsystem::GetLastPathIdentity(FGameplayTag QuestTag) const
{
	if (const FQuestEntryRecord* Record = QuestEntries.Find(QuestTag))
	{
		if (const FQuestEntryArrival* Latest = Record->GetLatest())
		{
			return Latest->PathIdentity;
		}
	}
	return NAME_None;
}

void UQuestStateSubsystem::RegisterQuestTag(FGameplayTag QuestTag)
{
	if (!QuestTag.IsValid()) return;

	// Idempotent on repeat calls. The inline-collision dedup case in RegisterQuestlineGraph (where a standalone
	// copy of an inlined graph would otherwise overwrite the parent-bound instance) skips re-registration via
	// LoadedNodeInstances.Contains, but defensive idempotency here keeps the registry correct if any future
	// caller pushes the same tag twice. Earliest RegisteredTime wins.
	if (KnownQuests.Contains(QuestTag)) return;

	FQuestRuntimeRecord& Record = KnownQuests.Add(QuestTag);
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* World = GI->GetWorld())
		{
			Record.RegisteredTime = World->GetTimeSeconds();
		}
	}

	UE_LOG(LogSimpleQuest, Verbose,
		TEXT("UQuestStateSubsystem::RegisterQuestTag : '%s' registered (KnownQuests count=%d, RegisteredTime=%.2fs)"),
		*QuestTag.ToString(), KnownQuests.Num(), Record.RegisteredTime);

	OnAnyRegistryChanged.Broadcast();
}

void UQuestStateSubsystem::UpdateQuestPrereqStatus(FGameplayTag QuestTag, const FQuestPrereqStatus& Status)
{
	if (!QuestTag.IsValid()) return;
	CachedPrereqStatus.Add(QuestTag, Status);
	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestStateSubsystem::UpdateQuestPrereqStatus : '%s' bSatisfied=%d (leaves=%d)"),
		*QuestTag.ToString(), Status.bSatisfied ? 1 : 0, Status.Leaves.Num());

	OnAnyRegistryChanged.Broadcast();
}

void UQuestStateSubsystem::ClearQuestPrereqStatus(FGameplayTag QuestTag)
{
	if (CachedPrereqStatus.Remove(QuestTag) > 0)
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestStateSubsystem::ClearQuestPrereqStatus : '%s' cleared"),
			*QuestTag.ToString());

		OnAnyRegistryChanged.Broadcast();
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

void UQuestStateSubsystem::RegisterContainerTag(FGameplayTag QuestTag)
{
	if (QuestTag.IsValid())
	{
		ContainerTags.Add(QuestTag);
	}
}

bool UQuestStateSubsystem::IsContainerTag(FGameplayTag QuestTag) const
{
	if (!QuestTag.IsValid()) return false;
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (ContainerTags.Contains(Tag)) return true;
	}
	return false;
}

void UQuestStateSubsystem::RegisterAlias(FGameplayTag AssetScopedTag, FGameplayTag ContextualTag)
{
	if (!AssetScopedTag.IsValid() || !ContextualTag.IsValid()) return;
	if (AssetScopedTag == ContextualTag) return;  // top-level content — no aliasing needed

	ContextualTagsByAssetScopedTag.FindOrAdd(AssetScopedTag).AddUnique(ContextualTag);
	AssetScopedAliasTagsByContextualTag.FindOrAdd(ContextualTag).AddUnique(AssetScopedTag);

	UE_LOG(LogSimpleQuest, Verbose,
		TEXT("UQuestStateSubsystem::RegisterAlias : '%s' → '%s' (forward index %d alias(es), reverse index %d contextual(s))"),
		*AssetScopedTag.ToString(), *ContextualTag.ToString(),
		ContextualTagsByAssetScopedTag.Num(), AssetScopedAliasTagsByContextualTag.Num());
}

TArray<FGameplayTag> UQuestStateSubsystem::ResolveCanonicalTags(FGameplayTag InputTag) const
{
	TArray<FGameplayTag> Result;
	if (!InputTag.IsValid()) return Result;

	// Always include the direct InputTag — it may be a ContextualTag with its own registry entries even when it
	// ALSO appears as a registered alias key (e.g., when both the home asset and a linking asset are active in
	// the same session, the home asset's standalone-form tag is a ContextualTag in the home compile AND an alias
	// key from the linking compile). Without this, the alias-walk shadows the direct lookup and any prereq leaf
	// referencing the home asset's standalone-form tag fails to see resolutions from the home's own compile.
	Result.Add(InputTag);

	// Alias case — append the canonical ContextualTags this alias represents. AddUnique avoids duplicates if
	// InputTag happens to also appear in the alias-walk results (e.g., self-aliasing edge cases).
	if (const TArray<FGameplayTag>* Contextuals = ContextualTagsByAssetScopedTag.Find(InputTag))
	{
		for (const FGameplayTag& Tag : *Contextuals)
		{
			Result.AddUnique(Tag);
		}
	}

	return Result;
}

