// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/ActivationGroupSetterNode.h"

#include "SimpleQuestLog.h"
#include "Events/QuestActivationGroupTriggeredEvent.h"
#include "Signals/SignalSubsystem.h"

void UActivationGroupSetterNode::ActivateInternal(FGameplayTag InContextualTag)
{
    // Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent.
    ContextualTag = InContextualTag;

    UGameInstance* GI = CachedGameInstance.Get();
    if (!GI || !GroupTag.IsValid())
    {
        ForwardActivation();
        return;
    }

    // Transient signal — payload threads PendingActivationParams + OriginChain reaching this Setter so
    // Listener-side stamping can mirror ChainToNextNodes::StampAndActivate. SourceTag is signal provenance only
    // (last entry on the inbound chain, NAME_None when no upstream chain exists). Group is transparent to chain
    // bookkeeping: Listener stamps OriginChain straight onto its destinations without appending its own tag.
    if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
    {
        const FName ProvenanceSource = PendingActivationParams.OriginChain.Num() > 0
            ? PendingActivationParams.OriginChain.Last().GetTagName()
            : NAME_None;

        FQuestActivationGroupTriggeredEvent Event(GroupTag, PendingActivationParams, ProvenanceSource, PendingActivationParams.OriginChain);
        Signals->PublishMessage(GroupTag, Event);

        UE_LOG(LogSimpleQuest, Verbose,
            TEXT("ActivationGroupSetter '%s' published transient signal — source='%s' chain-depth=%d"),
            *GroupTag.ToString(),
            *ProvenanceSource.ToString(),
            PendingActivationParams.OriginChain.Num());
    }

    ForwardActivation();
}