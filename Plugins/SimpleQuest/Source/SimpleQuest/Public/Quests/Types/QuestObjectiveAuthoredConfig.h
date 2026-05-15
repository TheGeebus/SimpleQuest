// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestObjectiveAuthoredConfig.generated.h"

class AActor;
class UQuestObjectiveConfig;

/**
 * Design-time configuration for an objective activation. The Authored layer of FQuestObjectiveActivationContext.
 * Composed from the destination Step node's UPROPERTYs at compile time. Immutable per activation — runtime
 * contributions (Forward Parameters, giver, publishers, external BP) go to the Dynamic layer instead.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveAuthoredConfig
{
	GENERATED_BODY()

	/** Actor classes the objective targets (for kill/pickup-class objectives). Designer-set on the Step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSet<TSoftClassPtr<AActor>> TargetClasses;

	/** Element count required to complete (for counting objectives). Designer-set on the Step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumElementsRequired = 0;

	/**
	 * Reference to a data asset carrying designer-authored reusable typed configuration. Subclass
	 * UQuestObjectiveConfig to define their schema, then create .uasset instances for value-set variation.
	 * The Step node's picker filters to UQuestObjectiveConfig and its descendants; the objective casts to the
	 * expected subclass at consumption time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UQuestObjectiveConfig> ConfigAsset;
};