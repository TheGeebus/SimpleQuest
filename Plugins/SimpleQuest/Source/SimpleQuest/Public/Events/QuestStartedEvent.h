#pragma once

#include "Events/QuestEventBase.h"

#include "QuestStartedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestStartedEvent : public FQuestEventBase
{	
	GENERATED_BODY()
	
	FQuestStartedEvent() = default;

	FQuestStartedEvent(const FGameplayTag InQuestTag)
		: FQuestEventBase(InQuestTag)
	{}

	FQuestStartedEvent(const FGameplayTag InQuestTag, const FQuestEventContext& InContext)
		: FQuestEventBase(InQuestTag, InContext)
	{}

	FQuestStartedEvent(const FGameplayTag InQuestTag, const FQuestEventContext& InContext, AActor* InGiverActor)
		: FQuestEventBase(InQuestTag, InContext), GiverActor(InGiverActor)
	{}

	/**
	 * Set when the quest was given via UQuestGiverComponent::GiveQuestByTag — points at the giver actor.
	 * Null when the quest started directly from an activation wire (no giver involvement).
	 */
	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<AActor> GiverActor;
};