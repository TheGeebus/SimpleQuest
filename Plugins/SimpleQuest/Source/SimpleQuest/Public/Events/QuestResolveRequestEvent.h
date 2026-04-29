// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "QuestResolveRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestResolveRequest)

/**
 * Publish on Tag_Channel_QuestResolveRequest to ask the manager to mark a quest Completed without exercising any
 * NextNodesByPath / NextNodesOnAnyOutcome chain. Forms the "manual lifecycle" trio with ActivateQuest and
 * DeactivateQuest, with designers polling prereqs and broadcasting outcomes themselves rather than driving completion
 * from a graph wire.
 *
 * bOverrideExisting (default false) guards against accidental double-broadcast: if the quest is already in a
 * terminal state (Completed or Deactivated), the manager skips with a warn-log. With true, the manager proceeds
 * additively: appends a new resolution entry to the quest's history; never RemoveFact's prior facts; never
 * overwrites prior records. OutcomeTag may be empty or invalid for "resolve without specifying an outcome."
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestResolveRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestResolveRequestEvent() = default;
	FQuestResolveRequestEvent(FGameplayTag InQuestTag, FGameplayTag InOutcomeTag, bool bInOverrideExisting)
		: FQuestEventBase(InQuestTag), OutcomeTag(InOutcomeTag), bOverrideExisting(bInOverrideExisting) {}

	UPROPERTY(BlueprintReadWrite)
	FGameplayTag OutcomeTag;

	UPROPERTY(BlueprintReadWrite)
	bool bOverrideExisting = false;
};