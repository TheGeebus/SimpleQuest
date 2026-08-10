// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Per-user editor memory of which sample source each mapping recipe was last authored against. This is deliberately NOT
// recipe data: a recipe describes a SHAPE and does not know where any file lives, which is what lets one recipe serve any
// conforming source. This only stops the panel from forgetting what you were doing between sessions — it is per-user and
// per-project, never travels with the asset, and a different user opening the same recipe simply has no sample until they
// point at one.

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "QuestResolverEditorMemo.generated.h"


/**
 * A questline's data endpoint, remembered per user. ONE struct rather than four parallel maps keyed by the same string:
 * a folder remembered without its format, or a stale table path left behind after a folder was picked, is exactly the
 * incoherent-combination failure that five hand-copied field assignments produced on the import side. An endpoint is
 * one value, so it is stored as one.
 */
USTRUCT()
struct FQuestResolverEndpointMemo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Folder;

	UPROPERTY()
	FString FormatName;

	/** Soft object path of the source Data Table, empty when the source is a folder. The two are exclusive. */
	UPROPERTY()
	FString Table;

	/**
	 * Soft object path of the translation recipe, empty for none. Not part of the endpoint proper - a recipe describes
	 * a SHAPE, not a location - but it belongs to the same "what was I doing with this questline" memory.
	 */
	UPROPERTY()
	FString Mapping;
};

UCLASS(config = EditorPerProjectUserSettings)
class UQuestResolverEditorMemo : public UObject
{
	GENERATED_BODY()

public:
	/** Mapping asset path -> the sample folder last used with it. */
	UPROPERTY(config)
	TMap<FString, FString> SampleFolderByMapping;

	/** Mapping asset path -> the sample format last used with it. */
	UPROPERTY(config)
	TMap<FString, FString> SampleFormatByMapping;

	/** Questline asset path -> the endpoint and recipe last used with it, so a session resumes where the last ended. */
	UPROPERTY(config)
	TMap<FString, FQuestResolverEndpointMemo> EndpointByQuestline;

	static UQuestResolverEditorMemo* Get() { return GetMutableDefault<UQuestResolverEditorMemo>(); }
};