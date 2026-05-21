// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

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
 * Primary subscribers: prereq enablement-watch and deferred-completion subscriptions for outcome-typed and
 * path-typed leaves. Outcome-typed leaves filter on OutcomeTag; path-typed leaves filter on PathIdentity.
 * Plain "I want to know when any quest resolves" consumers should keep using FQuestEndedEvent - same
 * ContextualTag channel, but with full FQuestEventPayload attached.
 */
USTRUCT(BlueprintType)
struct FQuestResolutionRecordedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestResolutionRecordedEvent() = default;

	/** Back-compat constructor — PathIdentity defaults to NAME_None, meaning "resolved without a specific path"
	 *  (typical for external ResolveQuest calls and wrapper boundary completions). New construction sites should
	 *  prefer the 5-arg form to preserve graph-authored path identity. */
	FQuestResolutionRecordedEvent(const FGameplayTag InQuestTag, const FGameplayTag InOutcomeTag,
		double InResolutionTime, EQuestResolutionSource InSource)
		: FQuestEventBase(InQuestTag), OutcomeTag(InOutcomeTag), ResolutionTime(InResolutionTime), Source(InSource) {}

	FQuestResolutionRecordedEvent(const FGameplayTag InQuestTag, const FGameplayTag InOutcomeTag,
		FName InPathIdentity, double InResolutionTime, EQuestResolutionSource InSource)
		: FQuestEventBase(InQuestTag), OutcomeTag(InOutcomeTag), PathIdentity(InPathIdentity),
		  ResolutionTime(InResolutionTime), Source(InSource) {}

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag OutcomeTag;

	/**
	 * The specific authored path the resolution fired through. Matches the source content node's output pin name
	 * (the pin's PathIdentity). NAME_None for resolutions that don't carry a path — external ResolveQuest calls,
	 * wrapper boundary completions, save-system rehydration. Path-keyed prereq subscribers filter on this field;
	 * outcome-keyed prereq subscribers can ignore it and read OutcomeTag.
	 */
	UPROPERTY(BlueprintReadOnly)
	FName PathIdentity = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	double ResolutionTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	EQuestResolutionSource Source = EQuestResolutionSource::Graph;
};