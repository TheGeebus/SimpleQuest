// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestContextBase.h"
#include "Quests/Types/QuestActivationProvenance.h"
#include "QuestRewardActivationContext.generated.h"

/**
 * What a reward reads when its node activates — "how the flow reached me." Inherits the lineage (Instigator,
 * CustomData, OriginTag, OriginChain, OriginatingEventID) from FQuestContextBase and adds the activation envelope
 * (Provenance + the outcome route that drove this activation). Distinct from FQuestObjectiveRuntimeContext, which
 * nests an objective Config a reward has no use for; the reward node builds this by slicing its PendingActivationContext.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestRewardActivationContext : public FQuestContextBase
{
	GENERATED_BODY()

	/** How this activation was initiated. Restored (a save reload) lets a future escrow reward suppress re-granting a banked portion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	EQuestActivationProvenance Provenance = EQuestActivationProvenance::Unknown;

	/** The outcome route that drove this activation, if any. A computed reward branches on it (combat vs diplomacy loot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag IncomingOutcomeTag;
};