// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/Types/QuestRewardContext.h"
#include "QuestRewardGrantedEvent.generated.h"

/**
 * Published on the reward-type channel when a reward is granted. Live-only by design — NOT a lifecycle event (not a
 * member of any catch-up reconstruction), so a recipient can always treat one as a real grant (no Delivery gate);
 * restore and late-join never re-fire it. Recipients subscribe to the reward-type channel (hierarchically — a
 * Currency.* subscriber catches Currency.Gold), self-filter via DoesRewardTargetMe, and react.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestRewardGrantedEvent
{
	GENERATED_BODY()

	/** The grant — type, recipient, payload (in CustomData), and lineage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FQuestRewardContext Grant;

	FQuestRewardGrantedEvent() = default;
	explicit FQuestRewardGrantedEvent(const FQuestRewardContext& InGrant) : Grant(InGrant) {}
};