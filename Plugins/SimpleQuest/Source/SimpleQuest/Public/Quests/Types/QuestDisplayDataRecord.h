// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestDisplayDataRecord.generated.h"

class UQuestDisplayData;

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

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TObjectPtr<UQuestDisplayData> DisplayData;

	/**
	 * The node's authored Order Bias, mirrored here so ordering can be answered from a TAG alone - the comparator
	 * works on tags, not on node instances, and the instances live on the manager rather than this registry.
	 *
	 * Written by its own setter rather than through RegisterDisplayData, because that function is called from four
	 * sites and a defaulted parameter would silently reset a node's bias to zero whenever any of them re-registered
	 * the same tag.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 OrderBias = 0;

	/**
	 * The node's position in the authored progression, mirrored from the compile. INDEX_NONE means this tag has no
	 * position at all - a questline asset's own identity tag is not a node in anybody's graph, so it has nowhere to
	 * sit. That distinction is load-bearing: positions from two different compiles are unrelated numbers, and the
	 * comparator only uses them when BOTH sides have one.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 AuthoredPosition = INDEX_NONE;

	/**
	 * Last position reached by this node's downstream run, mirrored from the compile. With AuthoredPosition it is the
	 * range this node occupies, and a range containing another's is what "this activates that" looks like once the
	 * graph is gone. INDEX_NONE wherever AuthoredPosition is.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 AuthoredSubtreeEnd = INDEX_NONE;
};