// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Signals/Types/SignalRoutingFlags.h"
#include "QuestObservedTagSpec.generated.h"

USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObservedTagSpec
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag Tag;

	/**
	 * Routing scope for an implicitly-observed tag's subscriptions. Bridge owners (Giver / Trigger)
	 * set this when their semantic requires narrow delivery — e.g., the Giver returns ExactMatch for
	 * its QuestTagsToGive entries because a Giver for "Quest X" doesn't care about Quest X's inner
	 * Step events. Default = ExactMatch | Descendants (matches Observer's hierarchical default).
	 */
	UPROPERTY(BlueprintReadOnly)
	ESignalRoutingFlags Routing = SignalRoutingDefaults::HierarchicalSubscribe;
};