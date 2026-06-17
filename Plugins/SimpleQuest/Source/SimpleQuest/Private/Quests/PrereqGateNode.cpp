// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/PrereqGateNode.h"

#include "SimpleQuestLog.h"

void UPrereqGateNode::ActivateInternal(FGameplayTag InContextualTag)
{
	// Intentionally skips Super — gate is a utility node and does not write Live or publish FQuestStartedEvent.
	// The prereq evaluation + deferral happens in Super::Activate; this method runs when prereqs are confirmed
	// satisfied (immediately at activation, or after deferred wake-up via the OnPrereq* handlers).

	// Per-event-ID dedup. When the same cascade event reaches the gate via both the prereq deferral path and
	// a direct Enter wire (Step whose completion both satisfies the gate's last prereq AND cascades into
	// Gate.Enter), the two paths arrive sequentially on a single call stack carrying the same OriginatingEventID.
	// Compare the most-recent incoming event ID (stashed by Activate from PendingActivationContext, or by the
	// prereq handlers from the event payload) against the gate's last-fired ID. Both-valid + matching = dedup.
	const FOriginatingEventID& Incoming = GetLastIncomingEventID();
	if (Incoming.IsValid() && Incoming == LastFiredEventID)
	{
		UE_LOG(LogSimpleQuestActivation, Verbose,
			TEXT("UPrereqGateNode: skipping redundant fire — already fired for this cascade event (eventGuid=%s, incoming=%s)"),
			*Incoming.AuthoredNodeGuid.ToString(EGuidFormats::Short),
			*InContextualTag.ToString());
		return;
	}
	LastFiredEventID = Incoming;

	UE_LOG(LogSimpleQuestActivation, Verbose,
		TEXT("UPrereqGateNode: forwarding activation (incoming=%s, eventGuid=%s)"),
		*InContextualTag.ToString(),
		Incoming.IsValid() ? *Incoming.AuthoredNodeGuid.ToString(EGuidFormats::Short) : TEXT("(invalid)"));
	ForwardActivation();
}

void UPrereqGateNode::ResetTransientState()
{
	Super::ResetTransientState();
	LastFiredEventID = FOriginatingEventID{};
}