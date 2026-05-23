// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Display/QuestDisplayData.h"
#include "QuestDisplayDataRecord.generated.h"


/**
 * Per-tag display data record stored by UQuestStateSubsystem. Mirrors the authored DisplayName / Description / DisplayData
 * fields baked onto the runtime UQuestNodeBase at compile time, but lives in QSS's registry so adopter queries don't reach
 * across the manager's opaque boundary. Manager pushes one record per registered ContextualTag (and parallel records under
 * each AssetScopedAliasTag) when a graph registers; cleared on graph unregister and PIE reset.
 */
USTRUCT(BlueprintType)
struct FQuestDisplayDataRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Display")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Display")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "Display")
	TObjectPtr<UQuestDisplayData> DisplayData;
};