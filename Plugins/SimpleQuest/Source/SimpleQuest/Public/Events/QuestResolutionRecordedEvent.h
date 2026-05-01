// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestResolutionRecord.h"

#include "QuestResolutionRecordedEvent.generated.h"

/**
 * Broadcast on UQuestStateSubsystem::RecordResolution, on the resolved quest's tag channel, after the resolution
 * entry has been appended to history. Distinct from FQuestEndedEvent (which is published by the manager during
 * graph-driven completion via ChainToNextNodes). This event fires for ALL resolution sources - graph chain,
 * external ResolveQuest calls, future save-system rehydration - so subscribers that need to hear about every
 * resolution regardless of path get a single canonical channel.
 *
 * Primary subscribers: prereq enablement-watch and deferred-completion subscriptions for outcome-typed leaves.
 * Each subscription filters on OutcomeTag in the payload to decide whether to re-evaluate. Plain "I want to
 * know when any quest resolves" consumers should keep using FQuestEndedEvent - same QuestTag channel, but
 * with full FQuestEventContext attached.
 */
USTRUCT(BlueprintType)
struct FQuestResolutionRecordedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestResolutionRecordedEvent() = default;

	FQuestResolutionRecordedEvent(const FGameplayTag InQuestTag, const FGameplayTag InOutcomeTag,
		double InResolutionTime, EQuestResolutionSource InSource)
		: FQuestEventBase(InQuestTag), OutcomeTag(InOutcomeTag), ResolutionTime(InResolutionTime), Source(InSource) {}

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag OutcomeTag;

	UPROPERTY(BlueprintReadOnly)
	double ResolutionTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	EQuestResolutionSource Source = EQuestResolutionSource::Graph;
};