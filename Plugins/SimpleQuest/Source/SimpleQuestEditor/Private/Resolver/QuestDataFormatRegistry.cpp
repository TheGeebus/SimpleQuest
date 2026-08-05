// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestDataFormatRegistry.h"

#include "Resolver/ISimpleQuestDataFormat.h"
#include "SimpleQuestLog.h"

FQuestDataFormatRegistry& FQuestDataFormatRegistry::Get()
{
	static FQuestDataFormatRegistry Instance;
	return Instance;
}

void FQuestDataFormatRegistry::RegisterFormat(const FString& Name, FQuestDataFormatFactoryDelegate Factory)
{
	const FString Key = Name.ToUpper();
	if (Key.IsEmpty() || !Factory.IsBound())
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("QuestDataFormatRegistry: refused to register format '%s' (empty name or unbound factory)."), *Name);
		return;
	}
	if (Factories.Contains(Key))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("QuestDataFormatRegistry: format '%s' already registered — replacing."), *Key);
	}
	Factories.Add(Key, MoveTemp(Factory));
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("QuestDataFormatRegistry: registered format '%s'."), *Key);
}

void FQuestDataFormatRegistry::UnregisterFormat(const FString& Name)
{
	Factories.Remove(Name.ToUpper());
}

bool FQuestDataFormatRegistry::IsRegistered(const FString& Name) const
{
	return Factories.Contains(Name.ToUpper());
}

TArray<FString> FQuestDataFormatRegistry::GetRegisteredNames() const
{
	TArray<FString> Names;
	Factories.GetKeys(Names);
	Names.Sort();
	return Names;
}

TUniquePtr<ISimpleQuestDataFormat> FQuestDataFormatRegistry::Create(const FString& Name) const
{
	const FQuestDataFormatFactoryDelegate* Factory = Factories.Find(Name.ToUpper());
	return (Factory && Factory->IsBound()) ? Factory->Execute() : nullptr;
}

void FQuestDataFormatRegistry::Reset()
{
	Factories.Reset();
}

FString ResolveQuestDataFormatName(const FString& ConsoleArgName, const FString& MappingAssetName,
                                   const FString& SettingsDefault, FString& OutError)
{
	OutError.Reset();
	const FQuestDataFormatRegistry& Registry = FQuestDataFormatRegistry::Get();

	// First non-empty source wins, most specific first. A named-but-unregistered format is a hard refusal, never a
	// silent fallback — a fallback would read the wrong on-disk layout. If no source names a format, use "TSV".
	const FString Ordered[] = { ConsoleArgName, MappingAssetName, SettingsDefault };
	for (const FString& Candidate : Ordered)
	{
		if (Candidate.IsEmpty()) continue;
		if (Registry.IsRegistered(Candidate)) return Candidate;
		OutError = FString::Printf(TEXT("format '%s' is not registered (registered: %s)"),
			*Candidate, *FString::Join(Registry.GetRegisteredNames(), TEXT(", ")));
		return FString();
	}
	return TEXT("TSV");
}