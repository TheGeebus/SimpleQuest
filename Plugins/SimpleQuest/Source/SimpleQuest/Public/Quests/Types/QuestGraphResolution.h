// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestGraphResolution.generated.h"

/**
 * Per-path attribution of a questline-asset resolution. When a content node's outcome cascade (or a utility
 * node's Forward output) terminates at an Exit/Outcome node at the asset's root scope, the questline resolves
 * with the Exit's authored OutcomeTag — distinct from the cascading path's outcome (typically the Step's pin
 * name), which is routing-internal identity for per-path destination lookup.
 *
 * Captured by the compiler in ResolvePinToTags's Exit branch and stored on FQuestPathNodeList / UQuestNodeBase
 * sibling collections. Consumed by ChainToNextNodes and HandleOnNodeForwardActivated, which thread each
 * entry's OutcomeTag into PublishGraphResolutions so the questline's resolution registry + bus event use the
 * authored outcome rather than the upstream Step's routing identity.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestGraphResolution
{
	GENERATED_BODY()

	/** Questline asset identity tag whose root-scope Exit this resolution attributes to. */
	UPROPERTY(VisibleDefaultsOnly)
	FGameplayTag GraphTag;

	/** The Exit/Outcome terminal's authored OutcomeTag — the outcome the questline resolves WITH. */
	UPROPERTY(VisibleDefaultsOnly)
	FGameplayTag OutcomeTag;

	bool operator==(const FQuestGraphResolution& Other) const
	{
		return GraphTag == Other.GraphTag && OutcomeTag == Other.OutcomeTag;
	}
};