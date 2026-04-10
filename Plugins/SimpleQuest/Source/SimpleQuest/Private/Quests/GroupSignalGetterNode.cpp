// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/GroupSignalGetterNode.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"

void UGroupSignalGetterNode::ActivateInternal(FGameplayTag InContextualTag)
{
    // Intentionally skips Super — utility nodes do not write Active or publish FQuestStartedEvent.
    ContextualTag = InContextualTag;

    if (!GroupSignalTag.IsValid()) return;

    UGameInstance* GI = CachedGameInstance.Get();
    if (!GI) return;

    UWorldStateSubsystem* WS = GI->GetSubsystem<UWorldStateSubsystem>();
    USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>();
    if (!WS || !Signals) return;

    // Catch-up: if the setter already fired before this getter activated, forward immediately.
    if (WS->HasFact(GroupSignalTag))
    {
        ForwardActivation();
        return;
    }

    SignalSubscriptionHandle = Signals->SubscribeMessage<FWorldStateFactAddedEvent>(GroupSignalTag, this, &UGroupSignalGetterNode::OnGroupSignalFired);
}

void UGroupSignalGetterNode::OnGroupSignalFired(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    if (UGameInstance* GI = CachedGameInstance.Get())
    {
        if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
        {
            Signals->UnsubscribeMessage(GroupSignalTag, SignalSubscriptionHandle);
            SignalSubscriptionHandle = FDelegateHandle();
        }
    }
    ForwardActivation();
}

void UGroupSignalGetterNode::DeactivateInternal(FGameplayTag InContextualTag)
{
    if (SignalSubscriptionHandle.IsValid())
    {
        if (UGameInstance* GI = CachedGameInstance.Get())
        {
            if (USignalSubsystem* Signals = GI->GetSubsystem<USignalSubsystem>())
            {
                Signals->UnsubscribeMessage(GroupSignalTag, SignalSubscriptionHandle);
                SignalSubscriptionHandle = FDelegateHandle();
            }
        }
    }
    Super::DeactivateInternal(InContextualTag);
}
