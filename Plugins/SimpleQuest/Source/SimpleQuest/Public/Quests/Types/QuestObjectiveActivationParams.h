// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "QuestObjectiveActivationParams.generated.h"

/**
 * Activation-time parameters passed to UQuestObjective::OnObjectiveActivated when a Step activates. Symmetric with
 * FQuestObjectiveContext on the completion/trigger side — named fields for authoring-time data plus
 * FInstancedStruct CustomData for game-specific runtime extension.
 *
 * Sources of CustomData (per the broader design):
 *   - Designer-authored Step fields (Piece A, landed) → UQuestStep::ActivateInternal composes the params.
 *   - Manager API StartQuestWithParams (Piece B) → programmatic / procedural runtime supplies the struct.
 *   - Quest giver component (Piece C) → giver carries a per-give CustomData.
 *   - Step-to-step handoff (Piece D) → CompleteObjectiveWithOutcome extended to carry forward params.
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

	/** Scene-position target (for position-based objectives like GoTo). */
	UPROPERTY(BlueprintReadWrite)
	FVector TargetVector = FVector::ZeroVector;

	/** Element count required to complete (for counting objectives). */
	UPROPERTY(BlueprintReadWrite)
	int32 NumElementsRequired = 0;

	/** Actor that initiated this activation — giver, triggering actor, external system. Populated by Piece B / C
		when those land. Nullable. */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> ActivationSource;

	/** Type-erased extension point for game-specific activation-time data. Populated by procedural generators,
		dialogue systems, save/load rehydration, etc. Read via CustomData.Get<FYourType>() in subclass overrides. */
	UPROPERTY(BlueprintReadWrite)
	FInstancedStruct CustomData;
};