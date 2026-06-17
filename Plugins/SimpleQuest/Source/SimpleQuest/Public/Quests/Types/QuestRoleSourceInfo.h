// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/World.h"
#include "QuestRoleSourceInfo.generated.h"

class AActor;
class UActorComponent;

/**
 * Per-source descriptor returned by the QuestStateSubsystem source-query API and re-exposed via
 * USimpleQuestBlueprintLibrary's Get*ForTag surface. Answers "which Trigger / Giver / Observer in the world
 * handles this quest tag?" — designers consume to drive nav hints, world-map markers, in-PIE leads UX,
 * head-to-X arrows, and similar follow-ups from FQuestActivationBlocker payloads and from Objective self-
 * introspection helpers.
 *
 * The component + actor references are weak — GC'd entries return invalid and the query loop skips them. Once
 * a designer has either reference in hand, the standard engine APIs (GetActorLocation, GetActorBounds, etc.)
 * cover every downstream lookup; no per-source data duplication baked into the framework.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestRoleSourceInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest|Source")
	TWeakObjectPtr<UActorComponent> LiveComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Quest|Source")
	TWeakObjectPtr<AActor> LiveActor;

	/**
	 * The tag the query input alias-walked to. Equal to the query input for direct matches; equal to a
	 * canonical when the query input was an alias of it (or vice versa). Lets consumers display
	 * "matched via X" disambiguation without re-running canonical resolution.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Quest|Source")
	FGameplayTag MatchedVia;
};