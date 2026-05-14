// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestContextBase.h"
#include "Quests/Types/QuestActivationProvenance.h"
#include "QuestObjectiveRuntimeContext.generated.h"

class AActor;

/**
 * Runtime-accumulated context for an objective activation. The Dynamic layer of FQuestObjectiveActivationContext.
 * Populated from upstream contributions:
 *   - Forward Parameters from upstream step completion
 *   - Quest giver component published context
 *   - External event bus publishers (FQuestActivationRequestEvent, BP-callable injection)
 *   - Cascade — chain-extension fields stamped by ActivateNodeByTag
 *
 * Inherits Instigator, CustomData, OriginTag, OriginChain, OriginatingEventID from FQuestContextBase. Adds
 * Dynamic-specific fields below.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveRuntimeContext : public FQuestContextBase
{
	GENERATED_BODY()

	/**
	 * Specific actors in the scene the objective targets, contributed at runtime (from triggers, givers, or
	 * upstream-step Forward Parameters). Distinct from FQuestObjectiveAuthoredConfig::TargetClasses, which is
	 * designer-pinned class-level intent.
	 */
	UPROPERTY(BlueprintReadWrite)
	TSet<TSoftObjectPtr<AActor>> TargetActors;

	/**
	 * The outcome tag that triggered this activation when the activator is an upstream content node's named
	 * outcome. Invalid for non-outcome-driven activations (top-level entry, giver gate, external publish
	 * without specific outcome). Read by HandleOnNodeStarted's UQuest branch to drive inner-entry routing.
	 * Consumed and cleared after use.
	 */
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag IncomingOutcomeTag;

	/**
	 * How this activation was initiated. Stamped onto the destination's PendingActivationContext; preserved
	 * through the merge into ReceivedActivationContext so the live objective and the registry's start record
	 * both see consistent provenance. Read by FQuestEntryArrival::Provenance via the snapshot.
	 */
	UPROPERTY(BlueprintReadWrite)
	EQuestActivationProvenance Provenance = EQuestActivationProvenance::Unknown;
};