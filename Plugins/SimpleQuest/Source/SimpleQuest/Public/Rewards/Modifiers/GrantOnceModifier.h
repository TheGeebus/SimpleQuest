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
 * get this" - a journal still promising a payout the player already holds is a visible lie. The preview path carries the
 * same quest identity the grant path does, precisely so both halves can agree.
 *
 * THE TWO PATHS COMPARE DIFFERENTLY, AND THAT IS NOT AN INCONSISTENCY. A grant runs with its own resolution already
 * recorded, so a first grant sees a count of one; a preview is asked with nothing in flight, so any recorded resolution
 * means the payout has already happened. Both answer "has this been granted before now" - they just count from different
 * starting points, which is why each states its own comparison rather than sharing one constant.
 */
UCLASS(meta = (DisplayName = "Grant Once"))
class SIMPLEQUEST_API UGrantOnceModifier : public UQuestRewardModifier
{
	GENERATED_BODY()

protected:
	virtual bool ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) override;
	virtual bool ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating) override;

private:
	/**
	 * Resolutions recorded so far for the context's anchoring quest, or INDEX_NONE when that cannot be established (no
	 * anchor tag, or no reachable state subsystem). Deliberately silent - an unknown means different things to the two
	 * callers, and a preview can be asked every frame by a tooltip, so each logs at the severity its own path warrants.
	 */
	int32 GetResolutionCount(const FQuestRewardActivationContext& Context) const;
};