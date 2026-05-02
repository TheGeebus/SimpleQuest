// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/QuestEntryRecord.h"

#include "QuestEntryRecordedEvent.generated.h"

/**
 * Broadcast on UQuestStateSubsystem::RecordEntry, on the destination quest's tag channel, after the
 * entry record has been appended to history.
 *
 * Primary subscribers: prereq enablement-watch and deferred-completion subscriptions for Leaf_Entry
 * leaves (compiled by the editor's Entry-node specific outcome path via AddEntryLeaf). The PrereqLeaf-
 * Subscription helper routes Leaf_Entry leaves to this event channel automatically. Handlers receive
 * the IncomingOutcomeTag in the payload and re-evaluate the full expression which checks each leaf
 * internally via UQuestStateSubsystem::HasEnteredWith.
 */
USTRUCT(BlueprintType)
struct FQuestEntryRecordedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestEntryRecordedEvent() = default;

	FQuestEntryRecordedEvent(const FGameplayTag InQuestTag, const FGameplayTag InSourceQuestTag,
		const FGameplayTag InIncomingOutcomeTag, double InEntryTime)
		: FQuestEventBase(InQuestTag)
		, SourceQuestTag(InSourceQuestTag)
		, IncomingOutcomeTag(InIncomingOutcomeTag)
		, EntryTime(InEntryTime) {}

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag SourceQuestTag;

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag IncomingOutcomeTag;

	UPROPERTY(BlueprintReadOnly)
	double EntryTime = 0.0;
};