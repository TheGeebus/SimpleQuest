// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<AActor> TriggeredActor;

	/** Number of elements completed at the time of this trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 CurrentCount = 0;

	/** Number of elements required by the objective. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 RequiredCount = 0;

	/**
	 * Framework-stamped at trigger-fire time — the Trigger Component that initiated this fire. Rides through the
	 * objective boundary and back out on Response / Blocked publishes so the originating component can filter
	 * incoming events via pointer-equality regardless of any TriggeredActor / Instigator changes the objective's
	 * completion logic makes. BlueprintReadOnly — framework owns this field; UQuestObjective::ResolveTriggerContext
	 * forces it from the inbound fire's value at publish time, so adopter mutations are silently ignored.
	 */
	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<UActorComponent> OriginatingTriggerComponent;
};