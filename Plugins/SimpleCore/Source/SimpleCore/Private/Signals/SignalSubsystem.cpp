// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Signals/SignalSubsystem.h"

void USignalSubsystem::Deinitialize()
{
	bIsShuttingDown = true;
	for (auto& Pair : ObjectChannels) { Pair.Value.Clear(); }
	ObjectChannels.Empty();
	for (auto& Pair : TagChannels) { Pair.Value.Clear(); }
	TagChannels.Empty();
	Super::Deinitialize();
}

void USignalSubsystem::UnsubscribeTypedByTag(const FGameplayTag EventTag, const FDelegateHandle Handle)
{
	if (FSignalEventMulticast* Delegate = TagChannels.Find(EventTag))
	{
		Delegate->Remove(Handle);
	}
}

FSignalEventChannelKey USignalSubsystem::MakeKey(const UObject* Object, const UScriptStruct* Struct)
{
	return FSignalEventChannelKey(Object, Struct);
}
