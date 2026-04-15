// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Signals/SignalSubsystem.h"

void USignalSubsystem::Deinitialize()
{
	bIsShuttingDown = true;
	for (auto& Pair : TagChannels) { Pair.Value.Clear(); }
	TagChannels.Empty();
	Super::Deinitialize();
}

void USignalSubsystem::PublishRawMessage(const FGameplayTag Channel, const FInstancedStruct& Payload)
{
	if (bIsShuttingDown) return;

	TSet<FGameplayTag> VisitedTags;
	FGameplayTag CurrentTag = Channel;

	while (CurrentTag.IsValid())
	{
		if (VisitedTags.Contains(CurrentTag)) break;
		VisitedTags.Add(CurrentTag);

		if (FSignalEventMulticast* Delegate = TagChannels.Find(CurrentTag))
		{
			FSignalEventMulticast DelegateCopy = *Delegate;
			DelegateCopy.Broadcast(Channel, Payload);
		}
		CurrentTag = CurrentTag.RequestDirectParent();
	}
}

void USignalSubsystem::UnsubscribeMessage(const FGameplayTag Channel, const FDelegateHandle Handle)
{
	if (FSignalEventMulticast* Delegate = TagChannels.Find(Channel))
	{
		Delegate->Remove(Handle);
		UE_LOG(LogSimpleCore, Verbose, TEXT("Signal::Unsubscribe: channel='%s'"), *Channel.ToString());
	}
}
