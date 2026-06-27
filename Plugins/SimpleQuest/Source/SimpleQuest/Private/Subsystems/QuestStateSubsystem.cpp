// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Subsystems/QuestStateSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestEntryRecordedEvent.h"
#include "Events/QuestResolutionRecordedEvent.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/Types/QuestDisplayDataRecord.h"
#include "Objectives/QuestObjective.h"
#include "Display/QuestDisplayData.h"
#include "Quests/Types/QuestRoleSourceInfo.h"
#include "Quests/Types/SimpleQuestSaveSnapshot.h"
#include "Subsystems/SignalSubsystem.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestTagComposer.h"
#include "Subsystems/WorldStateSubsystem.h"


namespace
{
	/**
	 * State-side multichannel publish helper. Builds the channel set as canonical + each registered alias from
	 * the reverse-alias map and forwards to the bus's multichannel publish primitive. Treats the call
	 * as one event instance addressable under all channels — subscribers bound to any perspective receive once
	 * (default deduplication-on), with matched-channel attribution in the callback's first arg per the channels-route /
	 * payloads-decide contract. Event.QuestTag (set canonically by the caller's event constructor) stays invariant
	 * across deliveries; payload identity vs delivery metadata are kept distinct.
	 *
	 * Sibling pattern to FQuestPublish::OnAllNodeTags, but uses the state subsystem's tag-keyed alias map rather
	 * than per-node alias arrays — the state subsystem's events don't carry a node reference at this layer.
	 */
	template <typename EventType>
	void PublishWithAliases(USignalSubsystem* Signals, FGameplayTag CanonicalTag, const TMap<FGameplayTag, TArray<FGameplayTag>>& AliasReverseMap,
		EventType Event, TArrayView<const FGameplayTag> AdditionalChannels = {})
	{
		if (!Signals || !CanonicalTag.IsValid()) return;

		TArray<FGameplayTag> Channels;
		Channels.Add(CanonicalTag);
		if (const TArray<FGameplayTag>* Aliases = AliasReverseMap.Find(CanonicalTag))
		{
			for (const FGameplayTag& AliasTag : *Aliases)
			{
				if (AliasTag.IsValid()) Channels.Add(AliasTag);
			}
		}
		
		// Cross-cutting channels — for resolution / entry events these include the outcome tag (and incoming
		// outcome tag, respectively) so subscribers can bind by outcome semantically without filtering payloads
		// on a quest-tag-keyed subscription. Bus dedup-on-by-default collapses delivery to one callback per
		// subscriber when they bind on multiple matched channels.
		for (const FGameplayTag& Extra : AdditionalChannels)
		{
			if (Extra.IsValid()) Channels.Add(Extra);
		}

		Signals->PublishMessageOnChannels(MoveTemp(Channels), Event);
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

bool UQuestStateSubsystem::HasResolvedAtPath(FGameplayTag QuestTag, FName PathIdentity) const
{
	if (!QuestTag.IsValid() || PathIdentity.IsNone()) return false;
	for (const FGameplayTag& Tag : ResolveCanonicalTags(QuestTag))
	{
		if (const TSet<FName>* PathSet = ResolvedPathsByQuest.Find(Tag))
		{
			if (PathSet->Contains(PathIdentity)) return true;
		}
	}
	return false;
}

bool UQuestStateSubsystem::HasAnyQuestResolvedWith(FGameplayTag OutcomeTag) const
{
	if (!OutcomeTag.IsValid()) return false;
	for (const FGameplayTag& Recorded : ResolvedOutcomes)
	{
		if (Recorded.MatchesTag(OutcomeTag)) return true;
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

void UQuestStateSubsystem::RecordResolution(FGameplayTag QuestTag, FGameplayTag OutcomeTag, FName PathIdentity, double ResolutionTime, EQuestResolutionSource Source, const FOriginatingEventID& OriginatingEventID)
{
	if (!QuestTag.IsValid()) return;

	// Multi-perspective registry write: append the resolution entry at the canonical AND every AssetScopedAlias.
	// Symmetric with WorldState's multi-perspective fact write and the bus's multichannel publish — registry
	// iterators (QSV, future telemetry) see the resolution at every perspective without per-row alias-walking.
	// F.3's event-keyed deduplication gate in the cascade ensures one logical RecordResolution call per logical event;
	// the splay across perspectives is a single write fanned out, not multiple cascade-driven calls.
	ForEachPerspective(QuestTag, [&](FGameplayTag Perspective)
	{
		FQuestResolutionRecord& Record = QuestResolutions.FindOrAdd(Perspective);
		FQuestResolutionEntry& Entry = Record.History.Emplace_GetRef();
		Entry.OutcomeTag = OutcomeTag;
		Entry.PathIdentity = PathIdentity;
		Entry.ResolutionTime = ResolutionTime;
		Entry.Source = Source;

		// Index maintenance for HasResolvedWith (outcome-keyed). Skipped when OutcomeTag is invalid (the "resolve
		// without specifying an outcome" case via the BP ResolveQuest helper). TSet handles deduplication so repeat
		// resolutions with the same outcome don't bloat the set.
		if (OutcomeTag.IsValid())
		{
			ResolvedOutcomesByQuest.FindOrAdd(Perspective).Add(OutcomeTag);
		}

		// Index maintenance for HasResolvedAtPath (path-keyed). Skipped when PathIdentity is NAME_None (external
		// API resolves and graph-level wrapper completions don't carry a path). TSet handles deduplication for
		// repeat resolutions through the same path.
		if (!PathIdentity.IsNone())
		{
			ResolvedPathsByQuest.FindOrAdd(Perspective).Add(PathIdentity);
		}
	});
	
	// Session-wide flat outcome index (new) — one insert; perspectives don't affect the outcome itself
	if (OutcomeTag.IsValid())
	{
		ResolvedOutcomes.Add(OutcomeTag);
	}

	UE_LOG(LogSimpleQuestState, Log, TEXT("QuestResolutions: appended '%s' outcome='%s' path='%s' source=%s (resolution #%d at t=%.2fs)"),
		*QuestTag.ToString(),
		*OutcomeTag.ToString(),
		*PathIdentity.ToString(),
		Source == EQuestResolutionSource::External ? TEXT("External") : TEXT("Graph"),
		QuestResolutions.FindOrAdd(QuestTag).History.Num(),
		ResolutionTime);

	// Broadcast on the resolved quest's tag channel + each AssetScopedAliasTag + the outcome tag itself.
	// Outcome-channel publish lets subscribers bind on a specific outcome tag (or a parent outcome tag for
	// hierarchical fan-in) without needing to subscribe per-quest and filter payloads. Reputation systems,
	// achievement trackers, telemetry, and audio cue layers benefit from binding by outcome directly.
	// Bus deduplication means subscribers on both the quest channel and the outcome channel receive one callback.
	const TArray<FGameplayTag> OutcomeChannels = { OutcomeTag };
	PublishWithAliases(
		ResolveSignalSubsystem(),
		QuestTag,
		AssetScopedAliasTagsByContextualTag,
		FQuestResolutionRecordedEvent(QuestTag, OutcomeTag, PathIdentity, ResolutionTime, Source, OriginatingEventID),
		OutcomeChannels);

	OnAnyRegistryChanged.Broadcast();
}

FOriginatingEventID UQuestStateSubsystem::GetPathFactWriteEventID(FGameplayTag PathFactTag) const
{
	return PathFactWriteEventIDs.FindRef(PathFactTag);
}

void UQuestStateSubsystem::StampPathFactWriteEventID(FGameplayTag PathFactTag, const FOriginatingEventID& EventID)
{
	if (PathFactTag.IsValid() && EventID.IsValid())
	{
		PathFactWriteEventIDs.Add(PathFactTag, EventID);
	}
}

void UQuestStateSubsystem::ClearPathFactWriteEventID(FGameplayTag PathFactTag)
{
	PathFactWriteEventIDs.Remove(PathFactTag);
}

void UQuestStateSubsystem::RecordEntry(
	FGameplayTag QuestTag,
	FGameplayTag SourceQuestTag,
	FGameplayTag IncomingOutcomeTag,
	double EntryTime,
	EQuestActivationProvenance Provenance,
	const FQuestObjectiveActivationContext& ActivationParamsSnapshot,
	FName PathIdentity, const FOriginatingEventID& OriginatingEventID)
{
	if (!QuestTag.IsValid()) return;

	// Multi-perspective registry write — see RecordResolution for the symmetry rationale.
	ForEachPerspective(QuestTag, [&](FGameplayTag Perspective)
	{
		FQuestEntryRecord& Record = QuestEntries.FindOrAdd(Perspective);
		FQuestEntryArrival& Entry = Record.History.Emplace_GetRef();
		Entry.SourceQuestTag = SourceQuestTag;
		Entry.IncomingOutcomeTag = IncomingOutcomeTag;
		Entry.EntryTime = EntryTime;
		Entry.Provenance = Provenance;
		Entry.ActivationContextSnapshot = ActivationParamsSnapshot;
		Entry.PathIdentity = PathIdentity;

		if (IncomingOutcomeTag.IsValid())
		{
			EnteredOutcomesByQuest.FindOrAdd(Perspective).Add(IncomingOutcomeTag);
		}
	});

	const AActor* GiverActor = ActivationParamsSnapshot.Dynamic.Instigator.Get();
	UE_LOG(LogSimpleQuestState, Log,
		TEXT("QuestEntries: appended '%s' source='%s' outcome='%s' provenance=%s giver='%s' path='%s' targetActors=%d targetClasses=%d numRequired=%d (entry #%d at t=%.2fs)"),
		*QuestTag.ToString(),
		*SourceQuestTag.ToString(),
		*IncomingOutcomeTag.ToString(),
		*UEnum::GetValueAsString(Provenance),
		GiverActor ? *GiverActor->GetName() : TEXT("null"),
		*PathIdentity.ToString(),
		ActivationParamsSnapshot.Dynamic.TargetActors.Num(),
		ActivationParamsSnapshot.Authored.TargetClasses.Num(),
		ActivationParamsSnapshot.Authored.NumElementsRequired,
		QuestEntries.FindOrAdd(QuestTag).History.Num(),
		EntryTime);

	// Broadcast on the destination quest's tag channel + each AssetScopedAliasTag + the incoming outcome tag.
	// PrereqLeafSubscription consumers routed by Leaf_Entry listen here and trigger expression re-evaluation;
	// designers can also subscribe directly for cascade-attribution audit / logging or to react when ANY quest
	// is entered via a specific outcome route (matched at the outcome-tag channel). The event's payload
	// preserves the legacy (QuestTag, SourceQuestTag, IncomingOutcomeTag, EntryTime) shape — subscribers
	// wanting the new provenance / snapshot fields read the latest entry from the registry on receipt.
	// Bus deduplication collapses delivery to one callback per subscriber across the channel set.
	const TArray<FGameplayTag> IncomingOutcomeChannels = { IncomingOutcomeTag };
	PublishWithAliases(
		ResolveSignalSubsystem(),
		QuestTag,
		AssetScopedAliasTagsByContextualTag,
		FQuestEntryRecordedEvent(QuestTag, SourceQuestTag, IncomingOutcomeTag, EntryTime),
		IncomingOutcomeChannels);

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
	// prefix expect to enumerate the canonicals their subscription would legitimately reach via the bus's
	// hierarchical-walk semantic. Resolve alias-prefix matches to their underlying canonicals (the value side
	// of the forward map) so callers get a uniform canonical-tag set for fact lookups, instance lookups, and
	// any other key-by-canonical operation. AddUnique handles the case where a canonical reaches the result
	// via both the direct KnownQuests match (above) AND an alias-prefix match here.
	for (const TPair<FGameplayTag, TArray<FGameplayTag>>& Pair : ContextualTagsByAssetScopedTag)
	{
		if (Pair.Key.MatchesTag(Prefix))
		{
			for (const FGameplayTag& Canonical : Pair.Value)
			{
				Out.AddUnique(Canonical);
			}
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
			return Latest->ActivationContextSnapshot.Dynamic.Instigator.Get();
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

FQuestObjectiveActivationContext UQuestStateSubsystem::GetLastActivationParamsSnapshot(FGameplayTag QuestTag) const
{
	if (const FQuestEntryRecord* Record = QuestEntries.Find(QuestTag))
	{
		if (const FQuestEntryArrival* Latest = Record->GetLatest())
		{
			return Latest->ActivationContextSnapshot;
		}
	}
	return FQuestObjectiveActivationContext();
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

	UE_LOG(LogSimpleQuestState, Verbose,
		TEXT("UQuestStateSubsystem::RegisterQuestTag : '%s' registered (KnownQuests count=%d, RegisteredTime=%.2fs)"),
		*QuestTag.ToString(), KnownQuests.Num(), Record.RegisteredTime);

	OnAnyRegistryChanged.Broadcast();
}

void UQuestStateSubsystem::UpdateQuestPrereqStatus(FGameplayTag QuestTag, const FQuestPrereqStatus& Status)
{
	if (!QuestTag.IsValid()) return;

	ForEachPerspective(QuestTag, [&](FGameplayTag Perspective)
	{
		CachedPrereqStatus.Add(Perspective, Status);
	});
	UE_LOG(LogSimpleQuestState, Verbose, TEXT("UQuestStateSubsystem::UpdateQuestPrereqStatus : '%s' bSatisfied=%d (leaves=%d)"),
		*QuestTag.ToString(),
		Status.bSatisfied ? 1 : 0,
		Status.Leaves.Num());
	OnAnyRegistryChanged.Broadcast();
}

void UQuestStateSubsystem::ClearQuestPrereqStatus(FGameplayTag QuestTag)
{
	if (!QuestTag.IsValid()) return;

	ForEachPerspective(QuestTag, [&](FGameplayTag Perspective)
	{
		CachedPrereqStatus.Remove(Perspective);
	});

	OnAnyRegistryChanged.Broadcast();
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

	UE_LOG(LogSimpleQuestState, Verbose,
		TEXT("UQuestStateSubsystem::RegisterAlias : '%s' → '%s' (forward index %d alias(es), reverse index %d contextual(s))"),
		*AssetScopedTag.ToString(),
		*ContextualTag.ToString(),
		ContextualTagsByAssetScopedTag.Num(),
		AssetScopedAliasTagsByContextualTag.Num());
	
	// An alias IS a perspective tag in its own right — every alias must also appear in KnownQuests so any-perspective
	// queries resolve uniformly. Folded in here so the invariant is enforced at the API boundary rather than relying on
	// every caller to remember the pairing. RegisterQuestTag is idempotent (Contains guard); calling it on an already-
	// known alias is a no-op.
	RegisterQuestTag(AssetScopedTag);
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

TArray<FGameplayTag> UQuestStateSubsystem::GetAssetScopedAliasTagsForCanonical(FGameplayTag ContextualTag) const
{
	if (!ContextualTag.IsValid()) return {};
	if (const TArray<FGameplayTag>* Aliases = AssetScopedAliasTagsByContextualTag.Find(ContextualTag))
	{
		return *Aliases;
	}
	return {};
}

FText UQuestStateSubsystem::GetDisplayName(FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return FText::GetEmpty();
	if (const FQuestDisplayDataRecord* Record = DisplayDataByTag.Find(Tag))
	{
		return Record->DisplayName;
	}
	UE_LOG(LogSimpleQuestState, Warning,
		TEXT("UQuestStateSubsystem::GetDisplayName : no display-data record for tag '%s' — tag may be unregistered or compile may have missed it. Returning empty."),
		*Tag.ToString());
	return FText::GetEmpty();
}

FText UQuestStateSubsystem::GetDisplayDescription(FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return FText::GetEmpty();
	if (const FQuestDisplayDataRecord* Record = DisplayDataByTag.Find(Tag))
	{
		return Record->Description;
	}
	UE_LOG(LogSimpleQuestState, Warning,
		TEXT("UQuestStateSubsystem::GetDisplayDescription : no display-data record for tag '%s' — tag may be unregistered or compile may have missed it. Returning empty."),
		*Tag.ToString());
	return FText::GetEmpty();
}

UQuestDisplayData* UQuestStateSubsystem::GetDisplayData(FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return nullptr;
	if (const FQuestDisplayDataRecord* Record = DisplayDataByTag.Find(Tag))
	{
		return Record->DisplayData;
	}
	UE_LOG(LogSimpleQuestState, Warning,
		TEXT("UQuestStateSubsystem::GetDisplayData : no display-data record for tag '%s' — tag may be unregistered or compile may have missed it. Returning null."),
		*Tag.ToString());
	return nullptr;
}

FSimpleQuestSaveSnapshot UQuestStateSubsystem::CaptureSnapshot() const
{
	FSimpleQuestSaveSnapshot Snapshot;
	Snapshot.Version = FSimpleQuestSaveSnapshot::CurrentVersion;

	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
		{
			Snapshot.WorldFacts = WorldState->GetAllFacts();
		}
	}
	Snapshot.Resolutions = QuestResolutions;   // ActivationContextSnapshot copies in-memory but won't serialize (un-flagged)
	Snapshot.Entries = QuestEntries;
	return Snapshot;
}

bool UQuestStateSubsystem::ApplySnapshot(const FSimpleQuestSaveSnapshot& Snapshot)
{
	if (Snapshot.Version != FSimpleQuestSaveSnapshot::CurrentVersion)
	{
		// Session A is single-version; future versions migrate here. Best-effort restore for now.
		UE_LOG(LogSimpleQuestState, Warning, TEXT("ApplySnapshot: snapshot version %d != current %d — restoring best-effort"),
			Snapshot.Version, FSimpleQuestSaveSnapshot::CurrentVersion);
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldState->RestoreFacts(Snapshot.WorldFacts);   // Layer 1 — fires OnAnyFactChanged
		}
	}

	QuestResolutions = Snapshot.Resolutions;   // Layer 2 — direct overwrite of the histories
	QuestEntries = Snapshot.Entries;
	RebuildRegistryIndices();

	OnAnyRegistryChanged.Broadcast();           // the "registry mutated, refresh" signal
	UE_LOG(LogSimpleQuestState, Log, TEXT("ApplySnapshot: restored %d fact(s), %d resolution key(s), %d entry key(s)"),
		Snapshot.WorldFacts.Num(), Snapshot.Resolutions.Num(), Snapshot.Entries.Num());
	return true;
}

void UQuestStateSubsystem::RebuildRegistryIndices()
{
	ResolvedOutcomesByQuest.Reset();
	ResolvedPathsByQuest.Reset();
	ResolvedOutcomes.Reset();
	for (const TPair<FGameplayTag, FQuestResolutionRecord>& Pair : QuestResolutions)
	{
		for (const FQuestResolutionEntry& Entry : Pair.Value.History)
		{
			if (Entry.OutcomeTag.IsValid())
			{
				ResolvedOutcomesByQuest.FindOrAdd(Pair.Key).Add(Entry.OutcomeTag);
				ResolvedOutcomes.Add(Entry.OutcomeTag);
			}
			if (!Entry.PathIdentity.IsNone())
			{
				ResolvedPathsByQuest.FindOrAdd(Pair.Key).Add(Entry.PathIdentity);
			}
		}
	}

	EnteredOutcomesByQuest.Reset();
	for (const TPair<FGameplayTag, FQuestEntryRecord>& Pair : QuestEntries)
	{
		for (const FQuestEntryArrival& Arrival : Pair.Value.History)
		{
			if (Arrival.IncomingOutcomeTag.IsValid())
			{
				EnteredOutcomesByQuest.FindOrAdd(Pair.Key).Add(Arrival.IncomingOutcomeTag);
			}
		}
	}
}

void UQuestStateSubsystem::RegisterDisplayData(FGameplayTag Tag, const FText& InDisplayName, const FText& InDescription, UQuestDisplayData* InDisplayData)
{
	if (!Tag.IsValid())
	{
		return;
	}
	FQuestDisplayDataRecord& Record = DisplayDataByTag.FindOrAdd(Tag);
	Record.DisplayName = InDisplayName;
	Record.Description = InDescription;
	Record.DisplayData = InDisplayData;
}

void UQuestStateSubsystem::UnregisterDisplayDataForTags(const TArray<FGameplayTag>& Tags)
{
	for (const FGameplayTag& Tag : Tags)
	{
		DisplayDataByTag.Remove(Tag);
	}
}

void UQuestStateSubsystem::ClearDisplayDataRegistry()
{
	DisplayDataByTag.Empty();
}

// ── Source registry — query ────────────────────────────────────────────────────────────────────────────────

TArray<FQuestRoleSourceInfo> UQuestStateSubsystem::GetActiveTriggersForTag(FGameplayTag QueryTag) const
{
	return QueryRoleSources(QueryTag, TriggerSourcesByTag);
}

TArray<FQuestRoleSourceInfo> UQuestStateSubsystem::GetActiveGiversForTag(FGameplayTag QueryTag) const
{
	return QueryRoleSources(QueryTag, GiverSourcesByTag);
}

TArray<FQuestRoleSourceInfo> UQuestStateSubsystem::GetActiveObserversForTag(FGameplayTag QueryTag) const
{
	return QueryRoleSources(QueryTag, ObserverSourcesByTag);
}

UQuestObjective* UQuestStateSubsystem::GetActiveObjectiveForTag(FGameplayTag QueryTag) const
{
	for (const FGameplayTag& Key : BuildTagSynonymSet(QueryTag))
	{
		if (const TWeakObjectPtr<UQuestObjective>* Found = ActiveObjectivesByTag.Find(Key))
		{
			if (UQuestObjective* Live = Found->Get()) return Live;
		}
	}
	return nullptr;
}

// ── Source registry — write ────────────────────────────────────────────────────────────────────────────────

void UQuestStateSubsystem::RegisterTriggerSource(UActorComponent* Component, const FGameplayTagContainer& AuthoredTags)
{
	RegisterRoleSource(Component, AuthoredTags, TriggerSourcesByTag);
}

void UQuestStateSubsystem::RegisterGiverSource(UActorComponent* Component, const FGameplayTagContainer& AuthoredTags)
{
	RegisterRoleSource(Component, AuthoredTags, GiverSourcesByTag);
}

void UQuestStateSubsystem::RegisterObserverSource(UActorComponent* Component, const FGameplayTagContainer& AuthoredTags)
{
	RegisterRoleSource(Component, AuthoredTags, ObserverSourcesByTag);
}

void UQuestStateSubsystem::UnregisterTriggerSource(UActorComponent* Component, FGameplayTag Tag)
{
	UnregisterRoleSource(Component, Tag, TriggerSourcesByTag);
}

void UQuestStateSubsystem::UnregisterGiverSource(UActorComponent* Component, FGameplayTag Tag)
{
	UnregisterRoleSource(Component, Tag, GiverSourcesByTag);
}

void UQuestStateSubsystem::UnregisterObserverSource(UActorComponent* Component, FGameplayTag Tag)
{
	UnregisterRoleSource(Component, Tag, ObserverSourcesByTag);
}

void UQuestStateSubsystem::UnregisterAllRoleSources(UActorComponent* Component)
{
	if (!Component) return;

	auto StripFrom = [Component](TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>>& Map)
	{
		for (auto It = Map.CreateIterator(); It; ++It)
		{
			It.Value().RemoveAll([Component](const TWeakObjectPtr<UActorComponent>& Weak)
				{ return !Weak.IsValid() || Weak.Get() == Component; });
			if (It.Value().IsEmpty()) It.RemoveCurrent();
		}
	};
	StripFrom(TriggerSourcesByTag);
	StripFrom(GiverSourcesByTag);
	StripFrom(ObserverSourcesByTag);
}

void UQuestStateSubsystem::RegisterActiveObjective(UQuestObjective* Objective, const TArray<FGameplayTag>& TagSet)
{
	if (!Objective) return;
	for (const FGameplayTag& Tag : TagSet)
	{
		if (!Tag.IsValid()) continue;
		ActiveObjectivesByTag.Add(Tag, Objective);
	}
}

void UQuestStateSubsystem::UnregisterActiveObjective(UQuestObjective* Objective)
{
	if (!Objective) return;
	for (auto It = ActiveObjectivesByTag.CreateIterator(); It; ++It)
	{
		const TWeakObjectPtr<UQuestObjective>& Weak = It.Value();
		if (!Weak.IsValid() || Weak.Get() == Objective) It.RemoveCurrent();
	}
}

// ── Source registry — shared helpers ──────────────────────────────────────────────────────────────────────

TArray<FQuestRoleSourceInfo> UQuestStateSubsystem::QueryRoleSources(
	FGameplayTag QueryTag,
	const TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>>& SourceMap) const
{
	TArray<FQuestRoleSourceInfo> Results;
	if (!QueryTag.IsValid()) return Results;

	// Walk every synonym of QueryTag — canonical-or-alias, in either direction. Component registration at
	// BeginPlay can race ahead of the manager's lazy graph registration (WarmReachableGraphs cascade), leaving
	// the role-source registry holding only the authored form. Bidirectional walk at query time catches the
	// registered key regardless of which form the designer authored against.
	const TArray<FGameplayTag> KeysToCheck = BuildTagSynonymSet(QueryTag);

	TSet<TWeakObjectPtr<UActorComponent>> SeenComponents;
	for (const FGameplayTag& Key : KeysToCheck)
	{
		const TArray<TWeakObjectPtr<UActorComponent>>* Bucket = SourceMap.Find(Key);
		if (!Bucket) continue;
		for (const TWeakObjectPtr<UActorComponent>& Weak : *Bucket)
		{
			if (!Weak.IsValid()) continue;
			if (SeenComponents.Contains(Weak)) continue;
			SeenComponents.Add(Weak);

			FQuestRoleSourceInfo Info;
			Info.LiveComponent = Weak;
			Info.LiveActor = Weak->GetOwner();
			Info.MatchedVia = Key;
			Results.Add(MoveTemp(Info));
		}
	}
	return Results;
}

void UQuestStateSubsystem::RegisterRoleSource(
	UActorComponent* Component,
	const FGameplayTagContainer& AuthoredTags,
	TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>>& SourceMap)
{
	if (!Component || AuthoredTags.IsEmpty()) return;

	// Register only under the authored tag form. Bidirectional synonym walks at query time
	// (BuildTagSynonymSet) resolve the registered key regardless of which form the caller passes — keeping
	// registration uncoupled from manager graph-registration timing (which is lazy via WarmReachableGraphs and
	// may not have populated the alias index by the component's BeginPlay).
	const TWeakObjectPtr<UActorComponent> WeakComp(Component);
	for (const FGameplayTag& Authored : AuthoredTags)
	{
		if (!Authored.IsValid()) continue;
		TArray<TWeakObjectPtr<UActorComponent>>& Bucket = SourceMap.FindOrAdd(Authored);
		// Idempotent: replace any prior entry for the same component (re-register pattern preserves uniqueness).
		Bucket.RemoveAll([Component](const TWeakObjectPtr<UActorComponent>& Existing)
		{
			return !Existing.IsValid() || Existing.Get() == Component;
		});
		Bucket.Add(WeakComp);
	}
}

void UQuestStateSubsystem::UnregisterRoleSource(
	UActorComponent* Component,
	FGameplayTag Tag,
	TMap<FGameplayTag, TArray<TWeakObjectPtr<UActorComponent>>>& SourceMap)
{
	if (!Component || !Tag.IsValid()) return;

	// Registration keys under the authored tag form only (see RegisterRoleSource), so remove by that same form.
	if (TArray<TWeakObjectPtr<UActorComponent>>* Bucket = SourceMap.Find(Tag))
	{
		Bucket->RemoveAll([Component](const TWeakObjectPtr<UActorComponent>& Weak)
			{ return !Weak.IsValid() || Weak.Get() == Component; });
		if (Bucket->IsEmpty()) SourceMap.Remove(Tag);
	}
}

TArray<FGameplayTag> UQuestStateSubsystem::BuildTagSynonymSet(FGameplayTag QueryTag) const
{
	TArray<FGameplayTag> Out;
	if (!QueryTag.IsValid()) return Out;
	Out.Add(QueryTag);
	for (const FGameplayTag& Canonical : ResolveCanonicalTags(QueryTag))
	{
		Out.AddUnique(Canonical);
		for (const FGameplayTag& AliasTag : GetAssetScopedAliasTagsForCanonical(Canonical))
		{
			Out.AddUnique(AliasTag);
		}
	}
	return Out;
}

