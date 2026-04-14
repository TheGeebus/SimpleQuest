// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestNodeInfo.generated.h"

/**
 * Compiled display metadata for a quest graph node. Populated by the compiler (DisplayName) and resolved at runtime (QuestTag).
 * Self-contained so it can be embedded in event context structs without requiring a reference back to the originating node.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestNodeInfo
{
	GENERATED_BODY()

	/** The node's routing/identity tag. Resolved at runtime from the compiler-assigned FName. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	FGameplayTag QuestTag;

	/**
	 * Unsanitized display name from the editor node label. Safe for direct UI display. Preserves spaces, punctuation, and
	 * casing that the sanitized tag segment strips.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	FText DisplayName;
};