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

	explicit FQuestObjectiveTriggered(UObject* InTriggeredActor, UObject* InInstigator = nullptr)
		: TriggeredActor(InTriggeredActor), Instigator(InInstigator)
	{}

	/** The target actor that was triggered (victim, interact point, waypoint). */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> TriggeredActor;

	/** The actor that caused the trigger (killer, interactor). Null for passive triggers. */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> Instigator;
};

