// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestObjectiveAuthoredConfig.generated.h"

class AActor;
class UQuestObjectiveConfig;

/**
 * The objective-config shape: target classes, target actors, required element count, and a typed ConfigAsset blank
 * slate. Used two ways — packed from the destination Step's UPROPERTYs as the authored config handed to the objective,
 * and carried on FQuestObjectiveActivationContext::Config when a runtime caller (giver, forward params, external BP)
 * contributes one. The objective composes the Step's authored config with the caller's (see OnObjectiveActivated).
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveAuthoredConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TSet<TSoftClassPtr<AActor>> TargetClasses;
	
	/** Specific scene actors pinned on the Step. The objective composes these with the caller's TargetActors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TSet<TSoftObjectPtr<AActor>> TargetActors;

	/** Element count required to complete (for counting objectives). Designer-set on the Step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 NumElementsRequired = 0;

	/**
	 * Reference to a data asset carrying designer-authored reusable typed configuration. Subclass
	 * UQuestObjectiveConfig to define their schema, then create .uasset instances for value-set variation.
	 * The Step node's picker filters to UQuestObjectiveConfig and its descendants; the objective casts to the
	 * expected subclass at consumption time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TSoftObjectPtr<UQuestObjectiveConfig> ConfigAsset;
};