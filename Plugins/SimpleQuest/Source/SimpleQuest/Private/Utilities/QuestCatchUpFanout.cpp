// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestCatchUpFanout.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "Utilities/SignalChannelUtils.h"


namespace FQuestCatchUpFanout
{
	TArray<FGameplayTag> FQuestCatchUpFanout::EnumerateTagsForCatchUp(FGameplayTag SubscribedTag, const UQuestStateSubsystem* StateSubsystem, ESignalRoutingMode Routing)
	{
		if (!SubscribedTag.IsValid() || !StateSubsystem) return {};

		if (!FSignalRoutingDefaults::IncludesDescendants(Routing))
		{
			UE_LOG(LogSimpleQuestSubscription, Verbose,
				TEXT("FQuestCatchUpFanout::EnumerateTagsForCatchUp : '%s' ExactMatch routing — returning subscribed tag only"),
				*SubscribedTag.ToString());
			return { SubscribedTag };
		}

		TArray<FGameplayTag> CatchUpTags = StateSubsystem->GetQuestTagsUnderPrefix(SubscribedTag);

		// Mirror the live cascade's parent-first delivery order. GetQuestTagsUnderPrefix returns tags in TMap
		// iteration order (non-deterministic relative to the cascade); subscribers binding via Hierarchical
		// routing expect the asset/parent-tag event to land before descendants — which the live cascade does
		// naturally because PublishMessage on the questline tag completes its synchronous dispatch before
		// ActivateQuestlineGraph iterates entry tags and triggers content-node publishes. Sort by tag-string
		// length ascending so the subscribed tag (shortest under itself) lands first, then descendants in
		// depth order.
		CatchUpTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString().Len() < B.ToString().Len();
		});

		UE_LOG(LogSimpleQuestSubscription, Verbose,
			TEXT("FQuestCatchUpFanout::EnumerateTagsForCatchUp : '%s' Descendants routing — fanned out to %d known quest tag(s), sorted parent-first"),
			*SubscribedTag.ToString(), CatchUpTags.Num());

		return CatchUpTags;
	}
	
	FTagReconstruction FQuestCatchUpFanout::ReconstructTag(const FGameplayTag& CanonicalTag, const FGameplayTag& SubscribedTag,	UWorldStateSubsystem* WorldState, UQuestStateSubsystem* QuestState)
	{
		FTagReconstruction Out;
		if (!WorldState || !CanonicalTag.IsValid()) return Out;

		// MatchedChannel: [canonical, ...aliases] → best match for the subscribed tag — the same selection the live bus
		// dispatcher uses, so delegates see a consistent MatchedChannel across catch-up and live deliveries.
		TArray<FGameplayTag> ChannelSet;
		ChannelSet.Add(CanonicalTag);
		if (QuestState)
		{
			for (const FGameplayTag& AliasTag : QuestState->GetAssetScopedAliasTagsForCanonical(CanonicalTag))
			{
				ChannelSet.Add(AliasTag);
			}
		}
		Out.MatchedChannel = FSignalChannelUtils::PickBestMatchChannel(ChannelSet, SubscribedTag);

		// Payload: tag + CatchUp delivery + rich fields rehydrated from the persisted entry snapshot (the same activation
		// IncomingParams the live publish forwards via AssembleEventContext) and the display registry. CompletionTrigger
		// stays default — a reconstruction has no live trigger.
		Out.Payload.NodeInfo.QuestTag = CanonicalTag;
		Out.Payload.Delivery = EQuestEventDelivery::CatchUp;
		if (QuestState)
		{
			Out.Payload.NodeInfo.DisplayName = QuestState->GetDisplayName(CanonicalTag);
			Out.Payload.Instigator = QuestState->GetLastGiverActor(CanonicalTag);
			if (const FQuestEntryRecord* EntryRec = QuestState->GetQuestEntry(CanonicalTag))
			{
				if (const FQuestEntryArrival* Latest = EntryRec->GetLatest())
				{
					const FQuestObjectiveActivationParams& Snap = Latest->ActivationParamsSnapshot;
					Out.Payload.CustomData         = Snap.CustomData;
					Out.Payload.CustomTag          = Snap.CustomTag;
					Out.Payload.OriginTag          = Snap.OriginTag;
					Out.Payload.OriginChain        = Snap.OriginChain;
					Out.Payload.OriginatingEventID = Snap.OriginatingEventID;
				}
			}
		}

		// Fact probes. "Entered scope" (Activated) proves out of PendingGiver / Live / the append-only Completed anchor /
		// the append-only Started anchor; Started proves it went live (Live / Completed / Started) — Started closes the
		// reached-but-neither-live-nor-completed limbo the transient facts can't. Enabled is PendingGiver + prereq-satisfied.
		const FGameplayTag PendingFact   = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, EQuestStateLeaf::PendingGiver);
		const bool bIsPendingGiver = PendingFact.IsValid()   && WorldState->HasFact(PendingFact);
		const FGameplayTag LiveFact      = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, EQuestStateLeaf::Live);
		const bool bIsLive         = LiveFact.IsValid()      && WorldState->HasFact(LiveFact);
		const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, EQuestStateLeaf::Completed);
		const bool bIsCompleted    = CompletedFact.IsValid() && WorldState->HasFact(CompletedFact);
		const FGameplayTag StartedFact   = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, EQuestStateLeaf::Started);
		const bool bWasStarted     = StartedFact.IsValid()   && WorldState->HasFact(StartedFact);

		if (bIsPendingGiver && QuestState)
		{
			Out.PrereqStatus = QuestState->GetQuestPrereqStatus(CanonicalTag);
		}

		auto AddEvent = [&Out](EQuestLifecycleEventType Type)
		{
			FReconstructedEvent Ev;
			Ev.EventType = Type;
			Out.Events.Add(Ev);
		};

		if (bIsPendingGiver || bIsLive || bIsCompleted || bWasStarted)
		{
			AddEvent(EQuestLifecycleEventType::Activated);
		}
		if (bIsPendingGiver && Out.PrereqStatus.bSatisfied)
		{
			AddEvent(EQuestLifecycleEventType::Enabled);
		}
		if (bIsLive || bIsCompleted || bWasStarted)
		{
			FReconstructedEvent Ev;
			Ev.EventType = EQuestLifecycleEventType::Started;
			Ev.RecoveredGiver = QuestState ? QuestState->GetLastGiverActor(CanonicalTag) : nullptr;
			Out.Events.Add(Ev);
		}
		if (bIsCompleted)
		{
			FReconstructedEvent Ev;
			Ev.EventType = EQuestLifecycleEventType::Completed;
			if (QuestState)
			{
				if (const FQuestResolutionRecord* Record = QuestState->GetQuestResolution(CanonicalTag))
				{
					if (const FQuestResolutionEntry* Latest = Record->GetLatest())
					{
						Ev.OutcomeTag = Latest->OutcomeTag;
					}
				}
			}
			Out.Events.Add(Ev);
		}

		const FGameplayTag DeactivatedFact = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, EQuestStateLeaf::Deactivated);
		if (DeactivatedFact.IsValid() && WorldState->HasFact(DeactivatedFact))
		{
			AddEvent(EQuestLifecycleEventType::Deactivated);
		}
		const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, EQuestStateLeaf::Blocked);
		if (BlockedFact.IsValid() && WorldState->HasFact(BlockedFact))
		{
			AddEvent(EQuestLifecycleEventType::Blocked);
		}

		return Out;
	}
}
