#pragma once

#include "Events/QuestEventBase.h"

#include "QuestRegistrationEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestRegistrationEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestRegistrationEvent() = default;
	
	FQuestRegistrationEvent(const FName InQuestID, const TSubclassOf<UQuest>& InQuestClass, AActor* InOwningActor)
		: FQuestEventBase(InQuestID, InQuestClass), OwningActor(InOwningActor) {}

	UPROPERTY()
	TObjectPtr<AActor> OwningActor;
};
