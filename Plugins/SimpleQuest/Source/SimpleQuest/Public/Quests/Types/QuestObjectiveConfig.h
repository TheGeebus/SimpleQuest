// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "QuestObjectiveConfig.generated.h"

/**
 * Marker base class for designer-authored reusable typed objective configuration. Subclass this to define
 * typed data schemas (Damage, Health, other custom fields needed per game), then create .uasset instances
 * in the content browser for value-set variation. Step nodes reference one instance per Step via the
 * ConfigAsset UPROPERTY on FQuestObjectiveAuthoredConfig; the picker filters to descendants of this class.
 *
 * Recommended workflow:
 *   1. Define USomeGameObjectiveConfig : UQuestObjectiveConfig with typed UPROPERTYs.
 *   2. Create DA_ConfigSetA.uasset, DA_ConfigSetB.uasset, etc. as asset instances with different values.
 *   3. Step nodes pick one instance per Step via the ConfigAsset UPROPERTY.
 *   4. Objective consumes via Cast<USomeGameObjectiveConfig>(Context.Authored.ConfigAsset.LoadSynchronous()).
 *
 * Abstract so the marker base never becomes a usable asset itself — must subclass to add fields.
 */
UCLASS(BlueprintType, Abstract)
class SIMPLEQUEST_API UQuestObjectiveConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()
};