// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/ActivationGroupGetterNode.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"

void UActivationGroupGetterNode::ActivateInternal(FGameplayTag InContextualTag)
{
    // Intentionally skips Super — does not write Active or publish FQuestStartedEvent.
    ContextualTag = InContextualTag;

    if (!GroupTag.IsValid()) return;

    UGameInstance* GI = CachedGameInstance.Get();
    if (!GI) return;

    UWorldStateSubsystem* WS = GI->GetSubsystem<UWorldStateSubsystem>();
    USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>();
    if (!WS || !Signals) return;
    
    if (SignalSubscriptionHandle.IsValid())
    {
        Signals->UnsubscribeMessage(GroupTag, SignalSubscriptionHandle);
        SignalSubscriptionHandle.Reset();
    }
    
    if (WS->HasFact(GroupTag))
    {
        ForwardActivation();
        return;
    }

    SignalSubscriptionHandle = Signals->SubscribeMessage<FWorldStateFactAddedEvent>(GroupTag, this, &UActivationGroupGetterNode::OnGroupSignalFired);
}

void UActivationGroupGetterNode::OnGroupSignalFired(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    ForwardActivation();
}

