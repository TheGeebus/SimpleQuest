#pragma once

struct FSignalKeyBase
{
	
};

struct FSignalEventChannelKey : FSignalKeyBase
{
	TObjectKey<const UObject> SignalObject;
	TObjectKey<const UScriptStruct> EventStruct;

	FSignalEventChannelKey() = default;

	FSignalEventChannelKey(const UObject* InSignalObject, const UScriptStruct* InEventStruct)
		: SignalObject(InSignalObject), EventStruct(InEventStruct) {}

	bool operator==(const FSignalEventChannelKey& Other) const
	{
		return SignalObject == Other.SignalObject && EventStruct == Other.EventStruct;
	}
};

FORCEINLINE uint32 GetTypeHash(const FSignalEventChannelKey& Key)
{
	return HashCombineFast(GetTypeHash(Key.SignalObject), GetTypeHash(Key.EventStruct));
}