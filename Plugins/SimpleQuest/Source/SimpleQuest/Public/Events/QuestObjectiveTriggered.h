#pragma once

#include "NativeGameplayTags.h"
#include "Events/QuestEventBase.h"
#include "QuestObjectiveTriggered.generated.h"

// Routing channel for all objective trigger events. Pass as the Channel argument to PublishMessage.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestTarget)

USTRUCT(BlueprintType)
struct FQuestObjectiveTriggered
{
	GENERATED_BODY()

	FQuestObjectiveTriggered() = default;

	explicit FQuestObjectiveTriggered(UObject* InTriggeredActor, UObject* InInstigator = nullptr, const FInstancedStruct& InCustomData = FInstancedStruct())
		: TriggeredActor(InTriggeredActor), Instigator(InInstigator), CustomData(InCustomData)
	{}

	/** The target actor that was triggered (victim, interact point, waypoint). */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> TriggeredActor;

	/** The actor that caused the trigger (killer, interactor). Null for passive triggers. */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> Instigator;

	/**
	 * Type-erased extension point for designer-supplied trigger data. Routed through to FQuestObjectiveContext::CustomData
	 * by UQuestManagerSubsystem::CheckQuestObjectives so objectives can read game-specific context without bypassing the
	 * typed event pipeline. Empty by default.
	 */
	UPROPERTY(BlueprintReadWrite)
	FInstancedStruct CustomData;
};

