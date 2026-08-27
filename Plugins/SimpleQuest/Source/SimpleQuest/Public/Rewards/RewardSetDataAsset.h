// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RewardSetDataAsset.generated.h"

class UQuestRewardBase;

/**
 * A reward composition defined once and referenced from many places. Rewards are otherwise authored INLINE on the node
 * that grants them, so ten quests granting the same bundle means authoring it ten times - and changing that bundle
 * means editing ten graphs.
 *
 * THE COMPILER FLATTENS IT. A node holds a soft REFERENCE, and compilation deep-copies this asset's rewards into the
 * compiled node exactly as it already does for inline ones - so the runtime never sees this asset, the reward manifest
 * is unchanged, and there is no indirection for the advertisement query to resolve through. The cost is that editing a
 * shared set requires recompiling the questlines that reference it, which is already true of every authored change.
 *
 * A questline's data export describes that questline, so a referenced set appears there as a reference rather than as
 * its contents. Rewards left inline on a node export as they always have, so a node granting a shared bundle plus a
 * unique item still carries the unique one in its own data.
 */
UCLASS(BlueprintType)
class SIMPLEQUEST_API URewardSetDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The rewards this set grants, in order. Order is meaning - it is the grant sequence - and it is preserved through
	 * compilation. Each entry is a configured UQuestRewardBase instance, exactly as on a Grant Rewards node.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Reward")
	TArray<TObjectPtr<UQuestRewardBase>> Rewards;
};

