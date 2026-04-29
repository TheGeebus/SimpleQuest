// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
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

	UPROPERTY(BlueprintReadWrite)
	TSoftObjectPtr<UQuestlineGraph> Graph;
};