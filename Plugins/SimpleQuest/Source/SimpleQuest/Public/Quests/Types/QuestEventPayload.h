// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/Types/QuestContextBase.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "Quests/Types/QuestNodeInfo.h"
#include "QuestEventPayload.generated.h"

/**
 * Outbound payload that rides on quest event publishes. System-prepared package for external consumption by
 * BP delegates, K2 nodes, and subscription APIs. NOT used for inbound information feeding into the system —
 * that's FQuestObjectiveTriggerContext's role.
 * 
 * Inherits Instigator, CustomData, lineage (OriginTag, OriginChain, OriginatingEventID) from FQuestContextBase.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestEventPayload : public FQuestContextBase
{
	GENERATED_BODY()

	/** Compile-time display metadata for the originating node. Always populated from the node instance. */
	UPROPERTY(BlueprintReadOnly)
	FQuestNodeInfo NodeInfo;

	/**
	 * The trigger that caused this event, when applicable. For completion events, populated with the trigger
	 * context that resolved the objective. For events with no triggering action (top-level entries, lifecycle
	 * transitions driven by graph cascades), default-constructed.
	 */
	UPROPERTY(BlueprintReadOnly)
	FQuestObjectiveTriggerContext CompletionTrigger;
};