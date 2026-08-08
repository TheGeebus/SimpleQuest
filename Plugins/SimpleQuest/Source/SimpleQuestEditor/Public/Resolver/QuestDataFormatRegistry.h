// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The provider-selection seam: a name -> factory map so a data format is chosen by name at import/export time, and so a
// studio can register its own ISimpleQuestDataFormat provider from its own module with no edit to SimpleQuest. Formats are
// keyed (many coexist), unlike the compiler factory (a single override). The built-in TSV and JSON providers register
// through this same public interface at module startup, so the seam is exercised by its own defaults.

#include "CoreMinimal.h"

class ISimpleQuestDataFormat;

/** Produces a fresh provider instance per call (providers are cheap and hold no per-operation state; callers own the result). */
DECLARE_DELEGATE_RetVal(TUniquePtr<ISimpleQuestDataFormat>, FQuestDataFormatFactoryDelegate);

class SIMPLEQUESTEDITOR_API FQuestDataFormatRegistry
{
public:
	static FQuestDataFormatRegistry& Get();

	/**
	 * Name is case-folded to upper on both register and lookup, so "json" and "JSON" are one key. Re-registering an
	 * existing name replaces the prior factory and logs a warning.
	 */
	void RegisterFormat(const FString& Name, FQuestDataFormatFactoryDelegate Factory);
	void UnregisterFormat(const FString& Name);

	bool IsRegistered(const FString& Name) const;

	/** Registered names, sorted — for the "not registered" refusal message and the settings/panel format dropdown. */
	TArray<FString> GetRegisteredNames() const;

	/**
	 * A fresh provider for Name, or null if Name isn't registered. The caller refuses on null rather than defaulting:
	 * silently falling back to another format would read the wrong on-disk layout and produce a misleading partial asset.
	 */
	TUniquePtr<ISimpleQuestDataFormat> Create(const FString& Name) const;

	/** Drop all registrations (called at module shutdown). */
	void Reset();

private:
	TMap<FString, FQuestDataFormatFactoryDelegate> Factories;   // key = lowercased name
};

/**
 * Resolve a format name from the ordered selection sources, most specific first. An empty string from a source means that
 * source didn't specify a format. Returns the resolved (registered) name, or an empty string with OutError set when a
 * named format isn't registered — the caller then refuses. Selection order:
 *   1. Console argument (--format=), highest — pins the format explicitly regardless of the project default.
 *   2. A per-mapping format (empty until the import-mapping asset exists; the slot is reserved here now).
 *   3. The project default.
 * If all sources are empty, falls back to "tsv".
 */
SIMPLEQUESTEDITOR_API FString ResolveQuestDataFormatName(
	const FString& ConsoleArgName,
	const FString& MappingAssetName,
	const FString& SettingsDefault,
	FString& OutError);