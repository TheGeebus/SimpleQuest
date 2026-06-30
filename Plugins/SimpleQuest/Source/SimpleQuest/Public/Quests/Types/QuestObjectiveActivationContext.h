// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestContextBase.h"
#include "QuestObjectiveAuthoredConfig.h"
#include "QuestObjectiveActivationContext.generated.h"


USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveActivationContext : public FQuestContextBase
{
	GENERATED_BODY()

	/**
	 * Objective config the caller contributes at runtime — the same shape the Step authors. The objective composes
	 * this incoming Config with the Step's authored Config (see UQuestObjective::OnObjectiveActivated).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FQuestObjectiveAuthoredConfig Config;
};