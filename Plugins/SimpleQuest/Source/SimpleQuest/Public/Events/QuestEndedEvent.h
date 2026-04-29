#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestResolutionRecord.h"

#include "QuestEndedEvent.generated.h"

USTRUCT(BlueprintType)
struct FQuestEndedEvent : public FQuestEventBase
{	
	GENERATED_BODY()

	FQuestEndedEvent() = default;

	FQuestEndedEvent(const FGameplayTag InQuestTag, const FGameplayTag InOutcomeTag, EQuestResolutionSource InSource)
		: FQuestEventBase(InQuestTag), OutcomeTag(InOutcomeTag), Source(InSource) {}

	FQuestEndedEvent(const FGameplayTag InQuestTag, const FGameplayTag InOutcomeTag, EQuestResolutionSource InSource,
		const FQuestEventContext& InContext)
		: FQuestEventBase(InQuestTag, InContext), OutcomeTag(InOutcomeTag), Source(InSource) {}

	UPROPERTY(BlueprintReadWrite)
	FGameplayTag OutcomeTag;

	/**
	 * Origin of the resolution: graph-driven completion or external (ResolveQuest BP helper). Mirrors the Source field
	 * on FQuestDeactivatedEvent for symmetric audit semantics.
	 */
	UPROPERTY(BlueprintReadWrite)
	EQuestResolutionSource Source = EQuestResolutionSource::Graph;
};