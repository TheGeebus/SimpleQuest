#pragma once

#include "NativeGameplayTags.h"
#include "Events/QuestEventBase.h"
#include "QuestObjectiveTriggered.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestTarget)

USTRUCT(BlueprintType)
struct FQuestObjectiveTriggered : public FSignalEventBase
{
	GENERATED_BODY()

	FQuestObjectiveTriggered() = default;

	FQuestObjectiveTriggered(UObject* InTriggeredActor)
		: FSignalEventBase(Tag_Channel_QuestTarget)
		, TriggeredActor(InTriggeredActor) {}

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> TriggeredActor;
};
