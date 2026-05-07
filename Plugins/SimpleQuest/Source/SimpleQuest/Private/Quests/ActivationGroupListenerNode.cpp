// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/ActivationGroupListenerNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestActivationGroupTriggeredEvent.h"
#include "Signals/SignalSubsystem.h"


void UActivationGroupListenerNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — utility nodes do not publish FQuestStartedEvent. Group listeners do their work
	// via OnRegisteredWithManager (instance-lifetime signal subscription) and OnGroupSignalReceived; this override
	// exists only to suppress the base class's OnNodeStarted dispatch in case any path ever calls Activate on a
	// Listener instance.
}

void UActivationGroupListenerNode::OnRegisteredWithManager()
{
	if (!GroupTag.IsValid()) return;

	UGameInstance* GI = CachedGameInstance.Get();
	if (!GI) return;

	USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>();
	if (!Signals) return;

	// Defensive — ResetTransientState should have wiped any stale handle from the prior PIE session, but if
	// ActivateQuestlineGraph runs twice without a session boundary in between we'd end up double-subscribed.
	if (SignalSubscriptionHandle.IsValid())
	{
		Signals->UnsubscribeMessage(GroupTag, SignalSubscriptionHandle);
		SignalSubscriptionHandle.Reset();
	}

	SignalSubscriptionHandle = Signals->SubscribeMessage<FQuestActivationGroupTriggeredEvent>(
		GroupTag, this, &UActivationGroupListenerNode::OnGroupSignalReceived);

	UE_LOG(LogSimpleQuest, Verbose,
		TEXT("ActivationGroupListener subscribed to '%s'"), *GroupTag.ToString());
}

void UActivationGroupListenerNode::OnGroupSignalReceived(FGameplayTag Channel, const FQuestActivationGroupTriggeredEvent& Event)
{
	// Stamp the signal payload onto own PendingActivationParams. The manager's HandleOnNodeForwardActivated
	// (Step 3 fix) threads this onto each NextNodesOnForward destination, mirroring ChainToNextNodes::
	// StampAndActivate. OriginChain is preserved verbatim — group is transparent to chain bookkeeping, so
	// the Listener does NOT append its own tag. SourceTag from the event is informational only.
	PendingActivationParams = Event.ForwardParams;
	PendingActivationParams.OriginChain = Event.OriginChain;

	UE_LOG(LogSimpleQuest, Verbose,
		TEXT("ActivationGroupListener '%s' received signal — source='%s' chain-depth=%d"),
		*Event.GroupTag.ToString(),
		*Event.SourceTag.ToString(),
		Event.OriginChain.Num());

	ForwardActivation();
}

void UActivationGroupListenerNode::ResetTransientState()
{
	Super::ResetTransientState();
	// SignalSubscriptionHandle pointed at the prior PIE session's SignalSubsystem (now destroyed). Drop it
	// defensively so OnRegisteredWithManager's "already-subscribed" guard doesn't skip the new session's
	// subscribe call.
	SignalSubscriptionHandle.Reset();
}

void UActivationGroupListenerNode::BeginDestroy()
{
	if (SignalSubscriptionHandle.IsValid())
	{
		if (UGameInstance* GI = CachedGameInstance.Get())
		{
			if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
			{
				Signals->UnsubscribeMessage(GroupTag, SignalSubscriptionHandle);
			}
		}
		SignalSubscriptionHandle.Reset();
	}
	Super::BeginDestroy();
}