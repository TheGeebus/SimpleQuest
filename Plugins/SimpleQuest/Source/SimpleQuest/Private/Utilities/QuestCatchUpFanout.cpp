// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestCatchUpFanout.h"
#include "SimpleQuestLog.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/SignalChannelUtils.h"


namespace FQuestCatchUpFanout
{
	TArray<FGameplayTag> FQuestCatchUpFanout::EnumerateTagsForCatchUp(FGameplayTag SubscribedTag, const UQuestStateSubsystem* StateSubsystem, ESignalRoutingMode Routing)
	{
		if (!SubscribedTag.IsValid() || !StateSubsystem) return {};

		if (!FSignalRoutingDefaults::IncludesDescendants(Routing))
		{
			UE_LOG(LogSimpleQuestSubscription, Verbose,
				TEXT("FQuestCatchUpFanout::EnumerateTagsForCatchUp : '%s' ExactMatch routing - returning subscribed tag only"),
				*SubscribedTag.ToString());
			return { SubscribedTag };
		}

		TArray<FGameplayTag> CatchUpTags = StateSubsystem->GetQuestTagsUnderPrefix(SubscribedTag);

		// Mirror the live cascade's parent-first delivery order. GetQuestTagsUnderPrefix returns tags in TMap
		// iteration order (non-deterministic relative to the cascade); subscribers binding via Hierarchical
		// routing expect the asset/parent-tag event to land before descendants - which the live cascade does
		// naturally because PublishMessage on the questline tag completes its synchronous dispatch before
		// ActivateQuestlineGraph iterates entry tags and triggers content-node publishes. Sort by tag-string
		// length ascending so the subscribed tag (shortest under itself) lands first, then descendants in
		// depth order.
		CatchUpTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString().Len() < B.ToString().Len();
		});

		UE_LOG(LogSimpleQuestSubscription, Verbose,
			TEXT("FQuestCatchUpFanout::EnumerateTagsForCatchUp : '%s' Descendants routing - fanned out to %d known quest tag(s), sorted parent-first"),
			*SubscribedTag.ToString(),
			CatchUpTags.Num());

		return CatchUpTags;
	}
	
	FTagReconstruction FQuestCatchUpFanout::ReconstructTag(const FGameplayTag& CanonicalTag, const FGameplayTag& SubscribedTag,	UWorldStateSubsystem* WorldState, UQuestStateSubsystem* QuestState)
	{
		FTagReconstruction Out;
		if (!WorldState || !CanonicalTag.IsValid()) return Out;

		// MatchedChannel: [canonical, ...aliases] → best match for the subscribed tag - the same selection the live bus
		// dispatcher uses, so delegates see a consistent MatchedChannel across catch-up and live deliveries.
		TArray<FGameplayTag> ChannelSet;
		ChannelSet.Add(CanonicalTag);
		if (QuestState)
		{
			for (const FGameplayTag& AliasTag : QuestState->GetAssetScopedAliasTagsForCanonical(CanonicalTag))
			{
				ChannelSet.Add(AliasTag);
			}
			// A placement wrapper also publishes on the identity of the asset it placed (FQuestPublish::OnAllNodeTags adds
			// LinkedInnerIdentityTag as a channel). Mirror that here so a subscriber bound at the asset perspective is matched
			// on catch-up the way it is matched live.
			if (const FGameplayTag Identity = QuestState->GetPlacementIdentityForCanonical(CanonicalTag); Identity.IsValid())
			{
				ChannelSet.Add(Identity);
			}
		}
		Out.MatchedChannel = FSignalChannelUtils::PickBestMatchChannel(ChannelSet, SubscribedTag);

		// Payload: tag + CatchUp delivery + rich fields rehydrated from the persisted entry snapshot (the same activation
		// IncomingParams the live publish forwards via AssembleEventContext) and the display registry. CompletionTrigger
		// stays default - a reconstruction has no live trigger.
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

		// One read of the node's lifecycle, then the events that read supports, in canonical fire order. The phase decides
		// which RUN is replayed: a node offered again after an earlier run replays ACTIVATED (+ ENABLED), not that run's
		// STARTED; a repeatable quest running again replays ACTIVATED + STARTED, not its earlier COMPLETED. History stays on
		// the registry, where a consumer that wants it can ask.
		const FQuestPhaseSnapshot Phase = FQuestLifecycleQuery::GetPhase(WorldState, QuestState, CanonicalTag);

		if (Phase.Phase == EQuestPhase::Activated && QuestState)
		{
			Out.PrereqStatus = QuestState->GetQuestPrereqStatus(CanonicalTag);
		}

		auto AddEvent = [&Out](EQuestLifecycleEventType Type)
		{
			FReconstructedEvent Ev;
			Ev.EventType = Type;
			Out.Events.Add(Ev);
		};
		auto AddStarted = [&Out, QuestState, &CanonicalTag]()
		{
			FReconstructedEvent Ev;
			Ev.EventType = EQuestLifecycleEventType::Started;
			Ev.RecoveredGiver = QuestState ? QuestState->GetLastGiverActor(CanonicalTag) : nullptr;
			Out.Events.Add(Ev);
		};

		switch (Phase.Phase)
		{
		case EQuestPhase::Activated:
			AddEvent(EQuestLifecycleEventType::Activated);
			if (Phase.bEnabled) AddEvent(EQuestLifecycleEventType::Enabled);
			break;

		case EQuestPhase::Started:
			AddEvent(EQuestLifecycleEventType::Activated);
			AddStarted();
			break;

		case EQuestPhase::Completed:
		{
			AddEvent(EQuestLifecycleEventType::Activated);
			AddStarted();
			FReconstructedEvent Ev;
			Ev.EventType = EQuestLifecycleEventType::Completed;
			Ev.OutcomeTag = Phase.LatestOutcome;
			Out.Events.Add(Ev);
			break;
		}

		case EQuestPhase::Deactivated:
			// The interrupted run first, if there was one. A node blocked before it ever ran carries Deactivated alone.
			if (Phase.bHasStarted)
			{
				AddEvent(EQuestLifecycleEventType::Activated);
				AddStarted();
			}
			AddEvent(EQuestLifecycleEventType::Deactivated);
			break;

		case EQuestPhase::NotReached:
		default:
			break;
		}

		if (Phase.bBlocked)
		{
			AddEvent(EQuestLifecycleEventType::Blocked);
		}

		UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("FQuestCatchUpFanout::ReconstructTag : '%s' phase=%s started=%d resolved=%d blocked=%d -> %d event(s)"),
			*CanonicalTag.ToString(),
			*UEnum::GetValueAsString(Phase.Phase),
			Phase.bHasStarted ? 1 : 0,
			Phase.bHasResolved ? 1 : 0,
			Phase.bBlocked ? 1 : 0,
			Out.Events.Num());

		return Out;
	}
}
