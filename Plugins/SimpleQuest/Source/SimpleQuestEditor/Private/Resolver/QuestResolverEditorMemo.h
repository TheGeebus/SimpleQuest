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

	static UQuestResolverEditorMemo* Get() { return GetMutableDefault<UQuestResolverEditorMemo>(); }
};