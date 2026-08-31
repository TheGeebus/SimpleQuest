// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/Modifiers/QuestRewardModifier.h"
#include "GrantOnceModifier.generated.h"


/**
 * Reference modifier: drops a grant that has already been made once. Attach it to a reward that must fire on a FIRST
 * completion only - a "you finished it" payout - so replaying the same content cannot grind it.
 *
 * IT COUNTS RESOLUTIONS RATHER THAN RECORDING GRANTS. UQuestStateSubsystem already keeps an append-only resolution history
 * per quest, and the resolution is recorded BEFORE the rewards attached to it are granted - so a first grant sees a count of
 * exactly one and every repeat sees more. Nothing new is written down, and there is no second record to drift out of step
 * with the one that matters.
 *
 * NO PAYLOAD GATE, deliberately: HandlesPayload is left accepting anything, because whether a grant repeats has nothing to
 * do with what it carries. This gates currency, XP, loot and payload-less rewards identically.
 *
 * IT HIDES THE ADVERTISEMENT ONCE COLLECTED. A reward that will not be granted again must stop appearing in "do this,
 * get this" - a journal still promising a payout the player already holds is a visible lie.
 *
 * ONE COMPARISON SERVES BOTH PATHS, because GetCompletionCount normalizes them to the same reference point. An earlier
 * version carried two different thresholds to compensate for the preview reading one completion behind; that was a
 * workaround for a contract the base class should have been enforcing at the read, and it is gone.
 */
UCLASS(meta = (DisplayName = "Grant Once"))
class SIMPLEQUEST_API UGrantOnceModifier : public UQuestRewardModifier
{
	GENERATED_BODY()

protected:
	virtual bool ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) override;
	virtual void ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating) override;
	
};