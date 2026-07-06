// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestObjectiveActivationContext.h"
#include "QuestActivationProvenance.h"
#include "QuestObjectiveRuntimeContext.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveRuntimeContext
{
	GENERATED_BODY()

	/** The caller's raw activation input, verbatim — Instigator, CustomData, lineage, target sets, authored overrides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FQuestObjectiveActivationContext IncomingContext;

	/** How this activation was initiated. Framework-stamped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	EQuestActivationProvenance Provenance = EQuestActivationProvenance::Unknown;

	/** The outcome route that drove this activation, if any. Framework-stamped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag IncomingOutcomeTag;
};