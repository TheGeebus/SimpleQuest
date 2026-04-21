// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "QuestObjectiveContext.generated.h"

/**
 * Generic context struct used on both sides of the objective boundary.
 * 
 *  - Inbound: carries data from the trigger event into TryCompleteObjective.
 *  - Outbound: carries completion data from the objective onto quest events.
 *
 * Not every field is expected to be populated. Objectives fill what applies. Add universally-applicable fields here;
 * game-specific data goes in CustomData.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveContext
{
	GENERATED_BODY()

	/** The actor that was acted upon (killed enemy, reached waypoint, interacted NPC). */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> TriggeredActor;

	/** The actor that caused the trigger (killer, interactor). Null for trigger-only objectives. */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> Instigator;

	/** Number of elements completed at the time of this event. */
	UPROPERTY(BlueprintReadWrite)
	int32 CurrentCount = 0;

	/** Number of elements required by the objective. */
	UPROPERTY(BlueprintReadWrite)
	int32 RequiredCount = 0;

	/** Type-erased extension point for game-specific data. */
	UPROPERTY(BlueprintReadWrite)
	FInstancedStruct CustomData;
};