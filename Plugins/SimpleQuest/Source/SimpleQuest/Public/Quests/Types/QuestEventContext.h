// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestObjectiveContext.h"
#include "Quests/Types/QuestNodeInfo.h"
#include "QuestEventContext.generated.h"

/**
 * Assembled context for outbound quest events.
 *
 *  - NodeInfo       — design-time: display name, tag identity (compiler-populated)
 *  - CompletionContext — completion-time: payload from the objective (default-constructed for non-completion events)
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestEventContext
{
	GENERATED_BODY()

	/** Compiled display metadata. Always populated from the node instance. */
	UPROPERTY(BlueprintReadOnly)
	FQuestNodeInfo NodeInfo;

	/** Completion data from the objective. Default-constructed for non-completion events. */
	UPROPERTY(BlueprintReadOnly)
	FQuestObjectiveContext CompletionContext;
};