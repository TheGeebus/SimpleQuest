// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Quests/Types/OriginatingEventID.h"
#include "QuestContextBase.generated.h"

/**
 * Shared root for every quest context / payload struct that flows around the framework's event boundaries.
 * Carries the fields that apply uniformly regardless of direction (inbound trigger, outbound event payload,
 * objective activation context). Derived structs add fields specific to their role. The read-from and
 * write-into surfaces both consume context data with the same shared shape. Every direction inherits lineage,
 * instigator attribution, and untyped extension uniformly.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestContextBase
{
	GENERATED_BODY()

	/**
	 * Actor that caused this event/activation. Live weak reference — deliberately NOT SaveGame-flagged: a weak pointer
	 * resolves eagerly on load and nulls when the actor hasn't streamed in yet. The save-stable form is captured as a
	 * lazily-resolving soft reference at the save boundary (see FQuestEntryArrival::InstigatorRef).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TWeakObjectPtr<AActor> Instigator;

	/** Untyped extension point for game-specific data. Read via CustomData.Get<FYourType>(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FInstancedStruct CustomData;
	
	/**
	 * Typed single-tag extension point, often an Outcome tag or other simple path identifier. Designer-authored.
	 * The framework never interprets it; consumers read and branch on it. Use CustomData when you need richer
	 * or multi-field game data; use CustomTag when one tag is all you need.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag CustomTag;

	/**
	 * Immediate-origin tag. The giver's authored origin, the upstream step's tag on cascade, the publisher-
	 * supplied tag on external API. Designer escape hatch for "who activated me?" branching in BP. Equivalent
	 * to OriginChain.Last() when the chain is non-empty. Invalid when there's no meaningful source.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag OriginTag;

	/**
	 * Full activation history, oldest-first. [0] is where the chain started (giver / external seeder); subsequent
	 * entries are cascade / step-handoff extensions. Subscribers / objectives that need full-path awareness
	 * ("did this chain pass through step X?") read this instead of OriginTag. Empty when no origin information
	 * exists. Ordered list (not a tag container) — order is semantically meaningful.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FGameplayTag> OriginChain;

	/**
	 * Cascade event ID — multi-tag-stable identity of the gameplay event that originated this cascade. Minted
	 * at the originating Step's resolution; threaded through ChainToNextNodes onto every downstream context.
	 * Read by FireWrapperBoundaryCompletion's event-keyed deduplication gate. Default-constructed (invalid) for
	 * contexts that don't originate from a Step resolution (top-level entries, direct external API requests).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FOriginatingEventID OriginatingEventID;
};