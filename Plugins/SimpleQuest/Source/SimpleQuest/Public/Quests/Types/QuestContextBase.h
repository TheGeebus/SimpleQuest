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

	/** Actor that caused this event/activation. */
	UPROPERTY(BlueprintReadWrite)
	TWeakObjectPtr<AActor> Instigator;

	/** Untyped extension point for game-specific data. Read via CustomData.Get<FYourType>(). */
	UPROPERTY(BlueprintReadWrite)
	FInstancedStruct CustomData;

	/**
	 * Immediate-origin tag. The giver's authored origin, the upstream step's tag on cascade, the publisher-
	 * supplied tag on external API. Designer escape hatch for "who activated me?" branching in BP. Equivalent
	 * to OriginChain.Last() when the chain is non-empty. Invalid when there's no meaningful source.
	 */
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag OriginTag;

	/**
	 * Full activation history, oldest-first. [0] is where the chain started (giver / external seeder); subsequent
	 * entries are cascade / step-handoff extensions. Subscribers / objectives that need full-path awareness
	 * ("did this chain pass through step X?") read this instead of OriginTag. Empty when no origin information
	 * exists. Ordered list (not a tag container) — order is semantically meaningful.
	 */
	UPROPERTY(BlueprintReadWrite)
	TArray<FGameplayTag> OriginChain;

	/**
	 * Cascade event ID — multi-tag-stable identity of the gameplay event that originated this cascade. Minted
	 * at the originating Step's resolution; threaded through ChainToNextNodes onto every downstream context.
	 * Read by FireWrapperBoundaryCompletion's event-keyed deduplication gate. Default-constructed (invalid) for
	 * contexts that don't originate from a Step resolution (top-level entries, direct external API requests).
	 */
	UPROPERTY(BlueprintReadWrite)
	FOriginatingEventID OriginatingEventID;
};