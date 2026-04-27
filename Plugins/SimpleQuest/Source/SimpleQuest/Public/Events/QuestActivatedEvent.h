// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Events/QuestEventBase.h"
#include "Quests/Types/PrerequisiteExpression.h"

#include "QuestActivatedEvent.generated.h"

/**
 * Published the moment execution reaches a giver-gated quest. Always fires on first activation-wire arrival,
 * regardless of prereq state. Designers can use the carried PrereqStatus to decide UI status or other behaviors
 * (locked indicator vs ready indicator, dialogue tone, etc.).
 *
 * Distinct from FQuestEnabledEvent (which fires only when the quest is genuinely accept-ready, i.e., Activated
 * AND prereqs satisfy). Binding to OnQuestActivated gets "first notice" semantics; binding to OnQuestEnabled
 * gets "now actually startable" semantics. Pick whichever fits the present need; bind to both for the
 * "show locked indicator on activation, swap to ready indicator on enablement" flow.
 *
 * Fires for giver-gated quests only; non-giver quests skip Activated and go directly to Started.
 */
USTRUCT(BlueprintType)
struct FQuestActivatedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestActivatedEvent() = default;

	explicit FQuestActivatedEvent(const FGameplayTag InQuestTag)
		: FQuestEventBase(InQuestTag) {}

	FQuestActivatedEvent(const FGameplayTag InQuestTag, const FQuestEventContext& InContext, const FQuestPrereqStatus& InPrereqStatus)
		: FQuestEventBase(InQuestTag, InContext)
		, PrereqStatus(InPrereqStatus) {}

	/**
	 * Snapshot of the quest's prereq evaluation at the moment Activation fires. Designers branch on
	 * PrereqStatus.bSatisfied to decide whether to surface a "ready" or "locked" UI immediately, and read
	 * PrereqStatus.Leaves for contextual hints about which prereqs the player still needs to satisfy. 
	 */
	UPROPERTY(BlueprintReadOnly)
	FQuestPrereqStatus PrereqStatus;
};