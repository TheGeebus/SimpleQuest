// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/ActivationGroupSetterNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestActivationGroupTriggeredEvent.h"
#include "Subsystems/SignalSubsystem.h"

void UActivationGroupSetterNode::ActivateInternal(FGameplayTag InContextualTag)
{
    // Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent.

    UGameInstance* GI = CachedGameInstance.Get();
    if (!GI || !GroupTag.IsValid())
    {
        ForwardActivation();
        return;
    }

    // Transient signal: payload threads PendingActivationContext + OriginChain + OriginatingEventID reaching this
    // Setter so Listener-side stamping can mirror ChainToNextNodes::StampAndActivate. SourceTag is signal
    // provenance only (last entry on the inbound chain, NAME_None when no upstream chain exists). Group is
    // transparent to chain bookkeeping AND to event-ID bookkeeping: Listener propagates both straight onto its
    // destinations without appending or re-minting. OriginatingEventID was stamped onto our PendingActivation-
    // Params upstream by ChainToNextNodes::StampAndActivate (or default-constructed if we were activated
    // outside a cascade); pass it through so the wrapper-completion gate downstream sees the right identity.
    if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
    {
        const FName ProvenanceSource = PendingActivationContext.IncomingContext.OriginChain.Num() > 0
            ? PendingActivationContext.IncomingContext.OriginChain.Last().GetTagName()
            : NAME_None;

        FQuestActivationGroupTriggeredEvent Event(GroupTag, PendingActivationContext.IncomingContext, ProvenanceSource,
            PendingActivationContext.IncomingContext.OriginChain, PendingActivationContext.IncomingContext.OriginatingEventID);
        Signals->PublishMessage(GroupTag, Event);

        UE_LOG(LogSimpleQuestActivation, Verbose,
            TEXT("ActivationGroupSetter '%s' published transient signal — source='%s' chain-depth=%d eventGuid=%s"),
            *GroupTag.ToString(),
            *ProvenanceSource.ToString(),
            PendingActivationContext.IncomingContext.OriginChain.Num(),
            *PendingActivationContext.IncomingContext.OriginatingEventID.AuthoredNodeGuid.ToString(EGuidFormats::Short));
    }

    ForwardActivation();
}