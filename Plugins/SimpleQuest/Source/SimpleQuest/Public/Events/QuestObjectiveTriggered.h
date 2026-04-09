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

	explicit FQuestObjectiveTriggered(UObject* InTriggeredActor)
		: TriggeredActor(InTriggeredActor)
	{}

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> TriggeredActor;
};
