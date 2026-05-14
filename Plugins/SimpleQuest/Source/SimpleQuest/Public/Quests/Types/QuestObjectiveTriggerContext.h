// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/Types/QuestContextBase.h"
#include "QuestObjectiveTriggerContext.generated.h"

/**
 * Trigger-event data crossing the objective boundary. Used bidirectionally:
 *   - INBOUND: trigger event provides this to TryCompleteObjective.
 *   - OUTBOUND: completion event payload's CompletionTrigger field carries the trigger that resolved
 *     the objective.
 *
 * Inherits Instigator, CustomData, OriginTag, OriginChain, OriginatingEventID from FQuestContextBase.
 * Not every field is expected to be populated; only write / read what applies.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveTriggerContext : public FQuestContextBase
{
	GENERATED_BODY()

	/** The actor that was acted upon (killed enemy, reached waypoint, interacted NPC). */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> TriggeredActor;

	/** Number of elements completed at the time of this trigger. */
	UPROPERTY(BlueprintReadWrite)
	int32 CurrentCount = 0;

	/** Number of elements required by the objective. */
	UPROPERTY(BlueprintReadWrite)
	int32 RequiredCount = 0;
};