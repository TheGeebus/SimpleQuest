// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestContextBase.h"
#include "Quests/Types/QuestActivationProvenance.h"
#include "QuestRewardActivationContext.generated.h"

/**
 * What a reward reads when its node activates - "how the flow reached me." Inherits the lineage (Instigator,
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

	/**
	 * The quest or questline whose resolution this grant is attributed to - what a reward reads when it needs to know
	 * "has this been completed before?" rather than "what completed just now." Both grant paths fill it: the questline's
	 * own identity for questline-level rewards, the upstream node's for a Grant Rewards node reached by cascade.
	 *
	 * NOT the same question as OriginTag, which is lineage - where the flow came FROM. On a questline with two Exits the
	 * two disagree: a second run resolving through a different Step leaves that Step on its FIRST resolution while the
	 * questline is on its second. Anything sensitive to repeats has to read this one.
	 *
	 * Invalid when no resolution anchors the grant. A reward that needs it should say so rather than guess.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag ResolvingQuestTag;
};