// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "GameplayTagContainer.h"
#include "QuestObjectiveActivationParams.generated.h"


/**
 * Activation-time parameters passed to UQuestObjective::OnObjectiveActivated when a Step activates. Symmetric with
 * FQuestObjectiveContext on the completion/trigger side — named fields for authoring-time data plus
 * FInstancedStruct CustomData for game-specific runtime extension.
 *
 * Sources of these params (per the broader design):
 *   - Designer-authored Step fields (Piece A, landed) → UQuestStep::ActivateInternal composes the base params.
 *   - External event bus FQuestActivationRequestEvent (Piece B, landed) → programmatic / procedural / save-load /
 *     test-harness entry point; stamps a full params struct.
 *   - Quest giver component authored ActivationParams (Piece C) → placed-actor-authored payload including
 *     TargetActors / counts / CustomData / ActivationSource / OriginTag, rides FQuestGivenEvent to the manager.
 *   - Cascade (Piece C) → ActivateNodeByTag stamps OriginTag + OriginChain extension from IncomingSourceTag when
 *     a step activates another step through a graph connection.
 *   - Step-to-step handoff (Piece D, pending) → CompleteObjectiveWithOutcome extended to carry forward full params
 *     with OriginChain extended by the completing step's tag.
 *
 * Not every field is expected to be populated. Objectives read what applies.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveActivationParams
{
	GENERATED_BODY()

	/** Specific actors in the scene authored on the Step. */
	UPROPERTY(BlueprintReadWrite)
	TSet<TSoftObjectPtr<AActor>> TargetActors;

	/** Actor classes to target (for kill/pickup-class objectives). */
	UPROPERTY(BlueprintReadWrite)
	TSet<TSubclassOf<AActor>> TargetClasses;

	/** Element count required to complete (for counting objectives). */
	UPROPERTY(BlueprintReadWrite)
	int32 NumElementsRequired = 0;

	/** Actor that initiated this activation — giver, triggering actor, external system. Populated by Piece B / C
		when those land. Nullable. */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> ActivationSource;

	/**
	 * Immediate-origin tag for this activation: the giver's authored origin, the upstream step's tag on cascade,
	 * the publisher-supplied tag on Piece B. Designer escape hatch for "who activated me?" branching in BP.
	 * Equivalent to OriginChain.Last() when the chain is non-empty. Empty when there's no meaningful source.
	 */
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag OriginTag;

	/**
	 * Full activation history, oldest-first. [0] is where the chain started (giver / external seeder); subsequent
	 * entries are cascade / step-handoff extensions. Objectives that need full-path awareness ("did this chain pass
	 * through step X?") read this instead of OriginTag. Empty when no origin information exists.
	 */
	UPROPERTY(BlueprintReadWrite)
	TArray<FGameplayTag> OriginChain;

	/**
	 * Type-erased extension point for game-specific activation-time data. Populated by procedural generators,
	 * dialogue systems, save/load rehydration, etc. Read via CustomData.Get<FYourType>() in subclass overrides.
	 */
	UPROPERTY(BlueprintReadWrite)
	FInstancedStruct CustomData;
};