// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "NativeGameplayTags.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "QuestlineStartRequestEvent.generated.h"

class UQuestlineGraph;

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestlineStartRequest)

/**
 * Publish on Tag_Channel_QuestlineStartRequest to ask the manager to activate a UQuestlineGraph asset at runtime.
 * Manager async-loads the soft-pointer via FStreamableManager and calls ActivateQuestlineGraph on load completion.
 * Already-loaded graphs activate on the hot path immediately; cold graphs activate as soon as the load resolves
 * (typically same frame or next). BP-friendly counterpart to UQuestManagerSubsystem::ActivateQuestlineGraph
 * (private API), supporting dynamic questline activation outside the InitialQuestlines startup list. Codifies
 * the async-load pattern that 0.5.0's runtime asset loading pass will generalize across target-class and
 * target-actor refs (those still LoadSynchronous in HandleOnNodeStarted as of Bundle Z).
 *
 * Params carries optional activation context stamped onto the graph's entry activation (the first node activated
 * by ActivateQuestlineGraph). Empty default means "activate with no additional context." Merges with the entry
 * node's authored defaults the same way step-level ActivationContext does.
 *
 * Does NOT derive from FQuestEventBase: there is no quest tag at the questline-graph layer; the graph contains
 * many quests, each with their own tag. Channel routing is via the dedicated Tag_Channel_QuestlineStartRequest
 * channel only.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestlineStartRequestEvent
{
	GENERATED_BODY()

	FQuestlineStartRequestEvent() = default;
	explicit FQuestlineStartRequestEvent(TSoftObjectPtr<UQuestlineGraph> InGraph) : Graph(InGraph) {}
	FQuestlineStartRequestEvent(TSoftObjectPtr<UQuestlineGraph> InGraph, const FQuestObjectiveActivationContext& InParams)
		: Graph(InGraph), Params(InParams) {}

	UPROPERTY(BlueprintReadWrite)
	TSoftObjectPtr<UQuestlineGraph> Graph;

	/** Optional activation context stamped onto the graph's entry activation. Empty default = no additional context. */
	UPROPERTY(BlueprintReadWrite)
	FQuestObjectiveActivationContext Params;
};