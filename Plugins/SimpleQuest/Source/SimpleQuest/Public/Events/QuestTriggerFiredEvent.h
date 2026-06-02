#pragma once

#include "NativeGameplayTags.h"
#include "Events/QuestEventBase.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "QuestTriggerFiredEvent.generated.h"

// Routing channel for all objective trigger events. Pass as the Channel argument to PublishMessage.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestTrigger)

USTRUCT(BlueprintType)
struct FQuestTriggerFiredEvent
{
	GENERATED_BODY()

	FQuestTriggerFiredEvent() = default;

	explicit FQuestTriggerFiredEvent(UObject* InTriggeredActor, UObject* InInstigator = nullptr, const FInstancedStruct& InCustomData = FInstancedStruct(),
		UActorComponent* InOriginatingTriggerComponent = nullptr, FGameplayTag InCustomTag = FGameplayTag())
		: TriggeredActor(InTriggeredActor), Instigator(InInstigator), CustomData(InCustomData), OriginatingTriggerComponent(InOriginatingTriggerComponent), CustomTag(InCustomTag)
	{}

	/** The target actor that was triggered (victim, interact point, waypoint). */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> TriggeredActor;

	/** The actor that caused the trigger (killer, interactor). Null for passive triggers. */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> Instigator;

	/**
	 * Type-erased extension point for designer-supplied trigger data. Routed through to FQuestObjectiveTriggerContext::CustomData
	 * by UQuestManagerSubsystem::CheckQuestObjectives so objectives can read game-specific context without bypassing the
	 * typed event pipeline. Empty by default.
	 */
	UPROPERTY(BlueprintReadWrite)
	FInstancedStruct CustomData;

	/**
	 * Framework-stamped at SendTriggerEvent publish time — the Trigger Component that initiated this fire. Used downstream to
	 * route Response / Blocked events back to the originating component via pointer-equality filtering, guaranteeing the
	 * publishing component receives feedback regardless of any TriggeredActor / Instigator mutation in the objective's
	 * TryCompleteObjective handler. BlueprintReadOnly — framework owns this field; setting it from adopter code is a no-op.
	 */
	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<UActorComponent> OriginatingTriggerComponent;

	/**
	 * Typed single-tag designer extension, routed through to FQuestObjectiveTriggerContext::CustomTag by
	 * UQuestManagerSubsystem::CheckQuestObjectives — the lightweight complement to CustomData for the common
	 * case where the trigger's game-specific data is a single tag (outcome routing, category, variant). Empty
	 * by default.
	 */
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag CustomTag;
};